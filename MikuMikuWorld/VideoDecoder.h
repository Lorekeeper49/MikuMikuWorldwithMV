#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace MikuMikuWorld
{
	enum class VideoDecoderStatus : uint8_t
	{
		Opening,
		Ready,
		Failed
	};

	// Decodes one video file on its own thread. The UI thread sets a target time and
	// collects the frame showing at that time as RGBA pixels whenever one is ready.
	class VideoDecoder
	{
	public:
		VideoDecoder(const std::string& filename, double initialTime);
		~VideoDecoder();

		VideoDecoder(const VideoDecoder&) = delete;
		VideoDecoder& operator=(const VideoDecoder&) = delete;

		void setTargetTime(double seconds);

		// Returns nullptr when no new frame has arrived. The pixels stay valid until the next call.
		const uint8_t* acquireFrame(double* framePts = nullptr);

		VideoDecoderStatus getStatus() const;

		// The values below are only meaningful once the status is Ready
		int getWidth() const;
		int getHeight() const;
		int getStride() const;
		float getDisplayAspect() const;

		// Zero while the length of the video is unknown
		double getDuration() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
