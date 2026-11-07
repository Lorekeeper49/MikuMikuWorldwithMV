#pragma once
#include "ImGui/imgui.h"
#include "Subtitles.h"
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	void drawSubtitleOverlay(ImDrawList* drawList, const ImVec2& areaMin, const ImVec2& areaMax,
		const std::vector<std::string>& lines, SubtitlePosition position);
}
