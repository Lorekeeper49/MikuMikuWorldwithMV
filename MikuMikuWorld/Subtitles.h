#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	enum class SubtitlePosition : uint8_t
	{
		Bottom,
		Right,
		Top,
		Left,
		SubtitlePositionMax
	};

	constexpr const char* subtitlePositions[]
	{
		"subtitle_position_bottom",
		"subtitle_position_right",
		"subtitle_position_top",
		"subtitle_position_left"
	};

	struct SubtitleCue
	{
		double start;
		double end;
		std::vector<std::string> lines;
	};

	std::vector<SubtitleCue> parseSubtitles(const std::vector<uint8_t>& fileBytes);

	class Subtitles
	{
	private:
		std::string filename{};
		std::vector<SubtitleCue> cues{};

	public:
		void load(const std::string& newFilename);

		const std::string& getFilename() const { return filename; }
		bool hasLoadFailed() const { return !filename.empty() && cues.empty(); }

		// Time is relative to the start of the subtitle file, not the chart
		std::vector<std::string> getLinesAt(double seconds) const;
	};
}
