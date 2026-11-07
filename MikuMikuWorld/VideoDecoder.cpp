#include "VideoDecoder.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

namespace MikuMikuWorld
{
	namespace
	{
		// Backgrounds don't need more than this, and it keeps texture uploads cheap for 4K sources
		constexpr int MAX_OUTPUT_WIDTH = 1920;
		constexpr int MAX_OUTPUT_HEIGHT = 1080;

		// Decoding forward is cheaper than seeking for short jumps
		constexpr double MAX_DECODE_AHEAD_SECONDS = 2.0;

		// Shows progress during long seeks and when decoding can't keep up with playback
		constexpr double PROGRESS_FRAME_INTERVAL_SECONDS = 0.033;

		// Some containers seek past the requested time, so retry from progressively earlier points
		constexpr int MAX_SEEK_RETRIES = 6;
		constexpr double FIRST_SEEK_RETRY_BACKOFF_SECONDS = 1.0;

		constexpr int OUTPUT_ROW_ALIGNMENT = 32;
		constexpr int MAX_DECODER_THREADS = 8;

		int roundToEven(double value)
		{
			return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
		}
	}

	struct VideoDecoder::Impl
	{
		std::string filename;

		std::mutex mutex;
		std::condition_variable wake;
		double target;
		std::atomic<bool> stopRequested{ false };
		std::thread thread;

		std::atomic<VideoDecoderStatus> status{ VideoDecoderStatus::Opening };
		std::atomic<double> duration{ 0.0 };
		int outputWidth{};
		int outputHeight{};
		int outputStride{};
		float displayAspect{ 1.0f };

		std::array<uint8_t*, 3> buffers{};
		uint8_t* back{};
		uint8_t* ready{};
		uint8_t* front{};
		bool readyIsNew{ false };
		double readyPts{};

		AVFormatContext* format{};
		AVCodecContext* codec{};
		SwsContext* sws{};
		AVPacket* packet{};
		AVFrame* current{};
		AVFrame* next{};
		int streamIndex{ -1 };
		double timeBase{};
		double startOffset{};
		double frameDuration{ 1.0 / 30.0 };

		bool currentValid{ false };
		bool nextValid{ false };
		double currentPts{};
		double nextPts{};
		double lastDecodedPts{};
		bool inputDrained{ false };
		bool hasDecodedAnyFrame{ false };

		double publishedPts{ -std::numeric_limits<double>::infinity() };
		std::chrono::steady_clock::time_point lastPublish{};

		Impl(const std::string& file, double initialTime) : filename{ file }, target{ std::max(initialTime, 0.0) } {}

		static int interruptCallback(void* opaque)
		{
			return static_cast<Impl*>(opaque)->stopRequested.load() ? 1 : 0;
		}

		double loadTarget()
		{
			std::lock_guard lock{ mutex };
			return target;
		}

		bool open()
		{
			format = avformat_alloc_context();
			if (!format)
				return false;

			format->interrupt_callback = { &Impl::interruptCallback, this };

			// Charts get shared, so a path stored in one must never reach beyond local files
			AVDictionary* options = nullptr;
			av_dict_set(&options, "protocol_whitelist", "file", 0);
			const int openResult = avformat_open_input(&format, filename.c_str(), nullptr, &options);
			av_dict_free(&options);
			if (openResult < 0)
				return false;

			if (avformat_find_stream_info(format, nullptr) < 0)
				return false;

			const AVCodec* decoder = nullptr;
			streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
			if (streamIndex < 0 || !decoder)
				return false;

			AVStream* stream = format->streams[streamIndex];

			// Cover art embedded in an audio file is not a video
			if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
				return false;

			for (unsigned int i = 0; i < format->nb_streams; ++i)
			{
				if (static_cast<int>(i) != streamIndex)
					format->streams[i]->discard = AVDISCARD_ALL;
			}

			codec = avcodec_alloc_context3(decoder);
			if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0)
				return false;

			const int threads = static_cast<int>(std::thread::hardware_concurrency()) / 2;
			codec->thread_count = std::clamp(threads, 2, MAX_DECODER_THREADS);
			codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
			codec->pkt_timebase = stream->time_base;
			if (avcodec_open2(codec, decoder, nullptr) < 0)
				return false;

			timeBase = av_q2d(stream->time_base);
			if (format->start_time != AV_NOPTS_VALUE)
				startOffset = static_cast<double>(format->start_time) / AV_TIME_BASE;

			const AVRational frameRate = av_guess_frame_rate(format, stream, nullptr);
			if (frameRate.num > 0 && frameRate.den > 0)
				frameDuration = av_q2d(av_inv_q(frameRate));

			if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
				duration = static_cast<double>(format->duration) / AV_TIME_BASE;
			else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
				duration = stream->duration * timeBase;

			const int sourceWidth = codec->width;
			const int sourceHeight = codec->height;
			if (sourceWidth <= 0 || sourceHeight <= 0)
				return false;

			const AVRational pixelAspect = av_guess_sample_aspect_ratio(format, stream, nullptr);
			const double pixelRatio = pixelAspect.num > 0 && pixelAspect.den > 0 ? av_q2d(pixelAspect) : 1.0;
			displayAspect = static_cast<float>(sourceWidth * pixelRatio / sourceHeight);

			const double scale = std::min({ 1.0, static_cast<double>(MAX_OUTPUT_WIDTH) / sourceWidth, static_cast<double>(MAX_OUTPUT_HEIGHT) / sourceHeight });
			outputWidth = roundToEven(sourceWidth * scale);
			outputHeight = roundToEven(sourceHeight * scale);
			outputStride = FFALIGN(outputWidth * 4, OUTPUT_ROW_ALIGNMENT);

			for (uint8_t*& buffer : buffers)
			{
				// The padding matches what FFmpeg's own frame buffers give its SIMD converters
				buffer = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(outputStride) * outputHeight + AV_INPUT_BUFFER_PADDING_SIZE));
				if (!buffer)
					return false;
			}
			back = buffers[0];
			ready = buffers[1];
			front = buffers[2];

			packet = av_packet_alloc();
			current = av_frame_alloc();
			next = av_frame_alloc();
			return packet && current && next;
		}

		double framePts(const AVFrame* frame)
		{
			int64_t timestamp = frame->best_effort_timestamp;
			if (timestamp == AV_NOPTS_VALUE)
				timestamp = frame->pts;

			lastDecodedPts = timestamp == AV_NOPTS_VALUE ? lastDecodedPts + frameDuration : timestamp * timeBase - startOffset;
			return lastDecodedPts;
		}

		bool decodeFrame(AVFrame* output, double& pts)
		{
			while (!stopRequested)
			{
				const int received = avcodec_receive_frame(codec, output);
				if (received == 0)
				{
					pts = framePts(output);
					return true;
				}

				if (received == AVERROR_EOF || inputDrained)
				{
					// Containers don't always report a length, so learn it from the last frame
					if (duration <= 0.0 && received == AVERROR_EOF)
						duration = lastDecodedPts + frameDuration;
					return false;
				}

				const int read = av_read_frame(format, packet);
				if (read < 0)
				{
					avcodec_send_packet(codec, nullptr);
					inputDrained = true;
					continue;
				}

				if (packet->stream_index == streamIndex)
					avcodec_send_packet(codec, packet);

				av_packet_unref(packet);
			}

			return false;
		}

		bool seekContainer(double seconds)
		{
			const int64_t timestamp = std::llround((seconds + startOffset) / timeBase);
			if (avformat_seek_file(format, streamIndex, std::numeric_limits<int64_t>::min(), timestamp, timestamp, 0) < 0
				&& av_seek_frame(format, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
				return false;

			avcodec_flush_buffers(codec);
			inputDrained = false;
			currentValid = nextValid = false;
			av_frame_unref(current);
			av_frame_unref(next);
			return true;
		}

		bool seekAndDecodeFirstFrame(double seconds)
		{
			double seekPoint = seconds;
			double backoff = FIRST_SEEK_RETRY_BACKOFF_SECONDS;
			for (int attempt = 0; attempt <= MAX_SEEK_RETRIES; ++attempt)
			{
				// An unseekable file can still play from the start if nothing has been read yet
				const bool atStartOfFile = !currentValid && !hasDecodedAnyFrame;
				if (!seekContainer(seekPoint) && !atStartOfFile)
					return false;

				currentValid = decodeFrame(current, currentPts);
				hasDecodedAnyFrame |= currentValid;

				// Indexes are often built on decode order, so the keyframe found for a time
				// can display after it. Any later frame is the wrong picture, so step back.
				const bool landedTooLate = !currentValid || currentPts > seconds;
				if (!landedTooLate)
					return true;

				if (seekPoint <= 0.0)
				{
					rewindToFileStart();
					return currentValid;
				}

				seekPoint = std::max(seconds - backoff, 0.0);
				backoff *= 2.0;
			}

			return currentValid;
		}

		// Formats without an index (MPEG-TS, MPEG-PS) can miss the first frames when seeking
		// to time zero. Those support seeking by byte, which reaches the true beginning.
		void rewindToFileStart()
		{
			if (av_seek_frame(format, -1, 0, AVSEEK_FLAG_BYTE) < 0)
				return;

			avcodec_flush_buffers(codec);
			inputDrained = false;
			currentValid = decodeFrame(current, currentPts);
			hasDecodedAnyFrame |= currentValid;
		}

		void publish(const AVFrame* frame, double pts)
		{
			sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
				outputWidth, outputHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (!sws)
				return;

			// Without this, HD video converts with SD colors and looks subtly wrong
			const int colorspace = frame->colorspace != AVCOL_SPC_UNSPECIFIED ? static_cast<int>(frame->colorspace)
				: frame->height >= 720 ? static_cast<int>(AVCOL_SPC_BT709) : static_cast<int>(AVCOL_SPC_SMPTE170M);
			const int sourceFullRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
			sws_setColorspaceDetails(sws, sws_getCoefficients(colorspace), sourceFullRange,
				sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);

			uint8_t* const outputPlanes[4]{ back, nullptr, nullptr, nullptr };
			const int outputStrides[4]{ outputStride, 0, 0, 0 };
			sws_scale(sws, frame->data, frame->linesize, 0, frame->height, outputPlanes, outputStrides);

			{
				std::lock_guard lock{ mutex };
				std::swap(back, ready);
				readyIsNew = true;
				readyPts = pts;
			}

			publishedPts = pts;
			lastPublish = std::chrono::steady_clock::now();
		}

		void publishProgress()
		{
			const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - lastPublish;
			if (currentValid && currentPts != publishedPts && elapsed.count() >= PROGRESS_FRAME_INTERVAL_SECONDS)
				publish(current, currentPts);
		}

		void showTime(double seconds)
		{
			const bool holdingLastFrame = !nextValid && inputDrained;
			const bool canDecodeForward = currentValid && seconds >= currentPts
				&& (holdingLastFrame || (nextValid && seconds < nextPts) || seconds - currentPts <= MAX_DECODE_AHEAD_SECONDS);

			if (!canDecodeForward)
			{
				if (!seekAndDecodeFirstFrame(seconds))
					return;

				nextValid = decodeFrame(next, nextPts);
				publishProgress();
			}
			else if (!nextValid && !inputDrained)
			{
				nextValid = decodeFrame(next, nextPts);
			}

			while (nextValid && nextPts <= seconds && !stopRequested)
			{
				std::swap(current, next);
				currentPts = nextPts;
				av_frame_unref(next);
				nextValid = decodeFrame(next, nextPts);

				const double latest = loadTarget();
				if (latest != seconds)
				{
					if (latest < currentPts || latest - currentPts > MAX_DECODE_AHEAD_SECONDS)
					{
						publishProgress();
						return;
					}

					seconds = latest;
				}

				publishProgress();
			}

			if (currentValid && currentPts != publishedPts)
				publish(current, currentPts);
		}

		void run()
		{
			if (!open())
			{
				status = VideoDecoderStatus::Failed;
				return;
			}

			status = VideoDecoderStatus::Ready;

			double handled = std::numeric_limits<double>::quiet_NaN();
			while (!stopRequested)
			{
				double seconds;
				{
					std::unique_lock lock{ mutex };
					wake.wait(lock, [&] { return stopRequested.load() || target != handled; });
					seconds = target;
				}

				if (stopRequested)
					break;

				handled = seconds;
				showTime(seconds);

				if (!hasDecodedAnyFrame && !stopRequested)
				{
					status = VideoDecoderStatus::Failed;
					return;
				}
			}
		}

		void release()
		{
			av_frame_free(&current);
			av_frame_free(&next);
			av_packet_free(&packet);
			sws_freeContext(sws);
			avcodec_free_context(&codec);
			avformat_close_input(&format);
			for (uint8_t*& buffer : buffers)
				av_freep(&buffer);
		}
	};

	VideoDecoder::VideoDecoder(const std::string& filename, double initialTime) : impl{ std::make_unique<Impl>(filename, initialTime) }
	{
		av_log_set_level(AV_LOG_QUIET);
		impl->thread = std::thread(&Impl::run, impl.get());
	}

	VideoDecoder::~VideoDecoder()
	{
		{
			std::lock_guard lock{ impl->mutex };
			impl->stopRequested = true;
		}
		impl->wake.notify_all();
		impl->thread.join();
		impl->release();
	}

	void VideoDecoder::setTargetTime(double seconds)
	{
		{
			std::lock_guard lock{ impl->mutex };
			if (impl->target == seconds)
				return;

			impl->target = seconds;
		}
		impl->wake.notify_one();
	}

	const uint8_t* VideoDecoder::acquireFrame(double* framePts)
	{
		std::lock_guard lock{ impl->mutex };
		if (!impl->readyIsNew)
			return nullptr;

		std::swap(impl->ready, impl->front);
		impl->readyIsNew = false;
		if (framePts)
			*framePts = impl->readyPts;

		return impl->front;
	}

	VideoDecoderStatus VideoDecoder::getStatus() const { return impl->status; }
	int VideoDecoder::getWidth() const { return impl->outputWidth; }
	int VideoDecoder::getHeight() const { return impl->outputHeight; }
	int VideoDecoder::getStride() const { return impl->outputStride; }
	float VideoDecoder::getDisplayAspect() const { return impl->displayAspect; }
	double VideoDecoder::getDuration() const { return impl->duration; }
}
