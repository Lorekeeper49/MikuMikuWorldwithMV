#include "MusicVideo.h"
#include "File.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>

namespace MikuMikuWorld
{
	// Measured from in-game footage: about 270 stripes across the MV, darkest point ~83% of the brightest.
	// The depth is set above 0.17 because 6 px stripes are never sampled exactly at their peak and trough.
	static constexpr float STAGE_LINE_COUNT = 270.0f;
	static constexpr float STAGE_LINE_DEPTH = 0.21f;
	// Below this many pixels per stripe the pattern aliases into moire, so use fewer, wider stripes
	static constexpr float STAGE_LINE_MIN_PIXELS = 3.0f;

	void getMusicVideoSize(float targetWidth, float targetHeight, float videoAspect, MusicVideoZoom zoom, float& width, float& height)
	{
		const float targetAspect = targetWidth / targetHeight;
		const bool matchWidth = zoom == MusicVideoZoom::Fit ? videoAspect >= targetAspect : videoAspect < targetAspect;
		width = matchWidth ? targetWidth : targetHeight * videoAspect;
		height = matchWidth ? targetWidth / videoAspect : targetHeight;
	}

	unsigned int getStageLinesTexture()
	{
		static unsigned int texture = 0;
		if (texture != 0)
			return texture;

		// Black with an alpha that follows one period of a cosine, so blending it over the video darkens it softly
		constexpr int width = 16;
		std::array<unsigned char, width * 4> pixels{};
		for (int x = 0; x < width; ++x)
		{
			const float phase = (x + 0.5f) / width * 2.0f * 3.14159265f;
			const float darkness = STAGE_LINE_DEPTH * (0.5f + 0.5f * std::cos(phase));
			pixels[x * 4 + 3] = static_cast<unsigned char>(std::lround(darkness * 255.0f));
		}

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		return texture;
	}

	float getStageLineCount(float videoWidthPixels)
	{
		// Whole-pixel stripe spacing; a fractional spacing drifts against the pixel grid and shows up as moire bands
		const float pixelsPerLine = std::max(STAGE_LINE_MIN_PIXELS, std::round(videoWidthPixels / STAGE_LINE_COUNT));
		return std::max(1.0f, videoWidthPixels / pixelsPerLine);
	}

	MusicVideo::~MusicVideo()
	{
		releaseTexture();
	}

	MusicVideo::MusicVideo(MusicVideo&& other) noexcept :
		filename{ std::move(other.filename) }, decoder{ std::move(other.decoder) },
		texture{ std::exchange(other.texture, 0) }, inRange{ other.inRange }, missingFile{ other.missingFile }
	{
	}

	MusicVideo& MusicVideo::operator=(MusicVideo&& other) noexcept
	{
		if (this != &other)
		{
			releaseTexture();
			filename = std::move(other.filename);
			decoder = std::move(other.decoder);
			texture = std::exchange(other.texture, 0);
			inRange = other.inRange;
			missingFile = other.missingFile;
		}

		return *this;
	}

	void MusicVideo::releaseTexture()
	{
		// The editor can outlive the OpenGL context during application shutdown
		if (texture != 0 && glfwGetCurrentContext())
			glDeleteTextures(1, &texture);

		texture = 0;
	}

	void MusicVideo::load(const std::string& newFilename)
	{
		decoder.reset();
		releaseTexture();
		filename = newFilename;
		inRange = false;
		missingFile = false;

		if (filename.empty())
			return;

		missingFile = !IO::File::exists(filename);
		if (!missingFile)
			decoder = std::make_unique<VideoDecoder>(filename, 0.0);
	}

	bool MusicVideo::hasLoadFailed() const
	{
		return missingFile || (decoder && decoder->getStatus() == VideoDecoderStatus::Failed);
	}

	float MusicVideo::getDisplayAspect() const
	{
		return decoder ? decoder->getDisplayAspect() : 1.0f;
	}

	void MusicVideo::update(double seconds)
	{
		if (!decoder || decoder->getStatus() != VideoDecoderStatus::Ready)
		{
			inRange = false;
			return;
		}

		const double duration = decoder->getDuration();
		inRange = seconds >= 0.0 && (duration <= 0.0 || seconds <= duration);
		if (inRange)
			decoder->setTargetTime(seconds);

		if (const unsigned char* pixels = decoder->acquireFrame())
			uploadFrame(pixels);
	}

	void MusicVideo::uploadFrame(const unsigned char* pixels)
	{
		const int width = decoder->getWidth();
		const int height = decoder->getHeight();
		const bool created = texture == 0;

		if (created)
		{
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, texture);
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, decoder->getStride() / 4);

		if (created)
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}
