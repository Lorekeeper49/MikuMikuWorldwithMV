#pragma once
#include "VideoDecoder.h"
#include <memory>
#include <string>

namespace MikuMikuWorld
{
	enum class MusicVideoZoom : uint8_t
	{
		Fill,
		Fit,
		MusicVideoZoomMax
	};

	constexpr const char* musicVideoZoomModes[]
	{
		"music_video_zoom_fill",
		"music_video_zoom_fit"
	};

	// Size of a video with the given aspect ratio once scaled into the target area.
	// Fill covers the whole area and crops the overflow; Fit shows the whole video and leaves bars.
	void getMusicVideoSize(float targetWidth, float targetHeight, float videoAspect, MusicVideoZoom zoom, float& width, float& height);

	// In game the MV is overlaid with fine, soft vertical stripes so it looks like a concert screen.
	// The texture is one stripe; tile it horizontally getStageLineCount() times across the video.
	unsigned int getStageLinesTexture();
	float getStageLineCount(float videoWidthPixels);

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
