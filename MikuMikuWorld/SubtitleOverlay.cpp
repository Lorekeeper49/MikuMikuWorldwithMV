#include "SubtitleOverlay.h"
#include "ImGui/imgui_internal.h"
#include <algorithm>
#include <cfloat>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr float FONT_SCALE = 1.75f;
		constexpr float MARGIN = 8.0f;
		constexpr float PADDING = 8.0f;
		constexpr float BOX_ROUNDING = 4.0f;
		constexpr float COLUMN_SPACING = 1.35f;
		constexpr ImU32 TEXT_COLOR = IM_COL32(255, 255, 255, 255);
		constexpr ImU32 BOX_COLOR = IM_COL32(0, 0, 0, 170);

		// A CJK em box spans 0.88em above the baseline to 0.12em below, so its center is 0.38em up
		constexpr float EM_CENTER_ABOVE_BASELINE = 0.38f;
		constexpr float PUNCTUATION_NUDGE = 0.55f;
		constexpr float SMALL_KANA_NUDGE = 0.08f;

		constexpr const char* FULL_WIDTH_REFERENCE = "\xE3\x81\x82";

		struct VerticalGlyph
		{
			const char* begin;
			const char* end;
			unsigned int codepoint;
			int column;
			float y;
			float advance;
		};

		struct TextRow
		{
			const char* begin;
			const char* end;
			float width;
		};

		// Without an OpenType 'vert' lookup, these only read correctly in a column when turned 90 degrees
		bool isSideways(unsigned int c)
		{
			if (c < 0x2000 || (c >= 0x2010 && c <= 0x2027))
				return true;

			if ((c >= 0x3008 && c <= 0x3011) || (c >= 0x3014 && c <= 0x301C))
				return true;

			switch (c)
			{
			case 0x3030: case 0x30FC:
			case 0xFF08: case 0xFF09: case 0xFF0D: case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
			case 0xFF3B: case 0xFF3D: case 0xFF5B: case 0xFF5D: case 0xFF5E: case 0xFF5F: case 0xFF60:
			case 0xFF62: case 0xFF63: case 0xFF70:
				return true;
			default:
				return false;
			}
		}

		float getUpperRightNudge(unsigned int c)
		{
			switch (c)
			{
			case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF61: case 0xFF64:
				return PUNCTUATION_NUDGE;

			case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063:
			case 0x3083: case 0x3085: case 0x3087: case 0x308E: case 0x3095: case 0x3096:
			case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3:
			case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6:
				return SMALL_KANA_NUDGE;

			default:
				return 0.0f;
			}
		}

		void rotateClockwise(ImDrawList* drawList, int firstVertex, float centerX, float centerY)
		{
			for (int i = firstVertex; i < drawList->VtxBuffer.Size; ++i)
			{
				ImVec2& pos = drawList->VtxBuffer[i].pos;
				const float dx = pos.x - centerX;
				const float dy = pos.y - centerY;
				pos.x = centerX - dy;
				pos.y = centerY + dx;
			}
		}

		void drawVertical(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& areaMin, const ImVec2& areaMax,
			const std::vector<std::string>& lines, SubtitlePosition position)
		{
			const float em = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, FULL_WIDTH_REFERENCE).x;
			const float columnWidth = em * COLUMN_SPACING;
			const float maxLength = std::max(em, areaMax.y - areaMin.y - (MARGIN + PADDING) * 2.0f);

			std::vector<VerticalGlyph> glyphs;
			int column = 0;
			float longest = 0.0f;
			for (const std::string& line : lines)
			{
				const char* textEnd = line.data() + line.size();
				float y = 0.0f;
				for (const char* s = line.data(); s < textEnd;)
				{
					unsigned int codepoint = 0;
					const char* next = s + ImTextCharFromUtf8(&codepoint, s, textEnd);
					const float advance = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s, next).x;

					if (y > 0.0f && y + advance > maxLength)
					{
						++column;
						y = 0.0f;
					}

					glyphs.push_back({ s, next, codepoint, column, y, advance });
					y += advance;
					longest = std::max(longest, y);
					s = next;
				}

				++column;
			}

			const float boxWidth = column * columnWidth + PADDING * 2.0f;
			const float boxHeight = longest + PADDING * 2.0f;
			const float boxLeft = position == SubtitlePosition::Left ? areaMin.x + MARGIN : areaMax.x - MARGIN - boxWidth;
			const float boxTop = areaMin.y + MARGIN;
			const float boxRight = boxLeft + boxWidth;

			drawList->AddRectFilled({ boxLeft, boxTop }, { boxRight, boxTop + boxHeight }, BOX_COLOR, BOX_ROUNDING);

			const float emCenterY = font->GetFontBaked(fontSize)->Ascent - em * EM_CENTER_ABOVE_BASELINE;
			for (const VerticalGlyph& glyph : glyphs)
			{
				const float nudge = getUpperRightNudge(glyph.codepoint) * em;
				const float centerX = boxRight - PADDING - columnWidth * (glyph.column + 0.5f) + nudge;
				const float centerY = boxTop + PADDING + glyph.y + glyph.advance * 0.5f - nudge;

				const int firstVertex = drawList->VtxBuffer.Size;
				drawList->AddText(font, fontSize, { centerX - glyph.advance * 0.5f, centerY - emCenterY }, TEXT_COLOR, glyph.begin, glyph.end);

				if (isSideways(glyph.codepoint))
					rotateClockwise(drawList, firstVertex, centerX, centerY);
			}
		}

		void drawHorizontal(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& areaMin, const ImVec2& areaMax,
			const std::vector<std::string>& lines, SubtitlePosition position)
		{
			const float em = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, FULL_WIDTH_REFERENCE).x;
			const float wrapWidth = std::max(em, areaMax.x - areaMin.x - (MARGIN + PADDING) * 2.0f);

			std::vector<TextRow> rows;
			float widest = 0.0f;
			for (const std::string& line : lines)
			{
				const char* textEnd = line.data() + line.size();
				for (const char* s = line.data(); s < textEnd;)
				{
					const char* rowEnd = font->CalcWordWrapPosition(fontSize, s, textEnd, wrapWidth);
					if (rowEnd <= s)
					{
						unsigned int codepoint = 0;
						rowEnd = s + ImTextCharFromUtf8(&codepoint, s, textEnd);
					}

					const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s, rowEnd).x;
					rows.push_back({ s, rowEnd, width });
					widest = std::max(widest, width);

					s = rowEnd;
					while (s < textEnd && *s == ' ')
						++s;
				}
			}

			const float centerX = (areaMin.x + areaMax.x) * 0.5f;
			const float boxWidth = widest + PADDING * 2.0f;
			const float boxHeight = rows.size() * fontSize + PADDING * 2.0f;
			const float boxTop = position == SubtitlePosition::Top ? areaMin.y + MARGIN : areaMax.y - MARGIN - boxHeight;

			drawList->AddRectFilled({ centerX - boxWidth * 0.5f, boxTop }, { centerX + boxWidth * 0.5f, boxTop + boxHeight }, BOX_COLOR, BOX_ROUNDING);

			float y = boxTop + PADDING;
			for (const TextRow& row : rows)
			{
				drawList->AddText(font, fontSize, { centerX - row.width * 0.5f, y }, TEXT_COLOR, row.begin, row.end);
				y += fontSize;
			}
		}
	}

	void drawSubtitleOverlay(ImDrawList* drawList, const ImVec2& areaMin, const ImVec2& areaMax,
		const std::vector<std::string>& lines, SubtitlePosition position)
	{
		if (lines.empty())
			return;

		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * FONT_SCALE;

		if (position == SubtitlePosition::Right || position == SubtitlePosition::Left)
			drawVertical(drawList, font, fontSize, areaMin, areaMax, lines, position);
		else
			drawHorizontal(drawList, font, fontSize, areaMin, areaMax, lines, position);
	}
}
