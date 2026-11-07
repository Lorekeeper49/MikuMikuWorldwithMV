#pragma once
#include "VideoDecoder.h"
#include <memory>
#include <string>

namespace MikuMikuWorld
{
	class MusicVideo
	{
	public:
		MusicVideo() = default;
		~MusicVideo();

		MusicVideo(MusicVideo&& other) noexcept;
		MusicVideo& operator=(MusicVideo&& other) noexcept;
		MusicVideo(const MusicVideo&) = delete;
		MusicVideo& operator=(const MusicVideo&) = delete;

		void load(const std::string& newFilename);

		// Call once per frame on the UI thread. Time is relative to the start of the video file.
		void update(double seconds);

		const std::string& getFilename() const { return filename; }
		bool hasLoadFailed() const;

		// False when the playbar is outside the video, so the normal background should show instead
		bool isVisible() const { return texture != 0 && inRange; }
		unsigned int getTexture() const { return texture; }
		float getDisplayAspect() const;

	private:
		std::string filename{};
		std::unique_ptr<VideoDecoder> decoder{};
		unsigned int texture{ 0 };
		bool inRange{ false };
		bool missingFile{ false };

		void releaseTexture();
		void uploadFrame(const unsigned char* pixels);
	};
}
