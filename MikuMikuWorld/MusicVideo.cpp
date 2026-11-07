#include "MusicVideo.h"
#include "File.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <utility>

namespace MikuMikuWorld
{
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
