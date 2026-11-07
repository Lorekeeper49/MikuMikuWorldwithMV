#include "Subtitles.h"
#include "File.h"
#include "IO.h"
#include <algorithm>
#include <string_view>
#include <Windows.h>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr unsigned int SHIFT_JIS_CODE_PAGE = 932;

		bool isDigit(char c)
		{
			return c >= '0' && c <= '9';
		}

		std::string_view trimAscii(std::string_view text)
		{
			constexpr std::string_view whitespace = " \t\r\n";
			const size_t first = text.find_first_not_of(whitespace);
			if (first == std::string_view::npos)
				return {};

			return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
		}

		std::vector<std::string_view> splitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			size_t start = 0;
			while (start <= text.size())
			{
				const size_t end = text.find_first_of("\r\n", start);
				if (end == std::string_view::npos)
				{
					lines.push_back(text.substr(start));
					break;
				}

				lines.push_back(text.substr(start, end - start));
				start = end + ((text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') ? 2 : 1);
			}

			return lines;
		}

		// Accepts [H:]MM:SS with an optional fraction of any precision, separated by '.' or ','
		bool parseTimestamp(std::string_view text, double& seconds)
		{
			double value = 0.0;
			int separators = 0;
			size_t i = 0;

			while (true)
			{
				const size_t fieldStart = i;
				double field = 0.0;
				while (i < text.size() && isDigit(text[i]))
					field = field * 10.0 + (text[i++] - '0');

				if (i == fieldStart)
					return false;

				value = value * 60.0 + field;
				if (i >= text.size() || text[i] != ':')
					break;

				++i;
				++separators;
			}

			if (separators == 0)
				return false;

			if (i < text.size() && (text[i] == '.' || text[i] == ','))
			{
				++i;
				double scale = 0.1;
				while (i < text.size() && isDigit(text[i]))
				{
					value += (text[i++] - '0') * scale;
					scale *= 0.1;
				}
			}

			seconds = value;
			return i == text.size();
		}

		std::string removeTags(std::string_view text, char open, char close)
		{
			std::string result;
			result.reserve(text.size());

			size_t i = 0;
			while (i < text.size())
			{
				if (text[i] == open)
				{
					const size_t tagEnd = text.find(close, i + 1);
					if (tagEnd != std::string_view::npos)
					{
						i = tagEnd + 1;
						continue;
					}
				}

				result.push_back(text[i++]);
			}

			return result;
		}

		std::vector<SubtitleCue> parseTimedTextCues(std::string_view text)
		{
			std::vector<SubtitleCue> cues;
			const std::vector<std::string_view> lines = splitLines(text);

			for (size_t i = 0; i < lines.size(); ++i)
			{
				const size_t arrow = lines[i].find("-->");
				if (arrow == std::string_view::npos)
					continue;

				std::string_view endText = trimAscii(lines[i].substr(arrow + 3));
				endText = endText.substr(0, endText.find_first_of(" \t"));

				SubtitleCue cue{};
				if (!parseTimestamp(trimAscii(lines[i].substr(0, arrow)), cue.start) || !parseTimestamp(endText, cue.end))
					continue;

				while (i + 1 < lines.size() && !trimAscii(lines[i + 1]).empty() && lines[i + 1].find("-->") == std::string_view::npos)
				{
					std::string line = removeTags(removeTags(trimAscii(lines[++i]), '<', '>'), '{', '}');
					if (!trimAscii(line).empty())
						cue.lines.push_back(std::move(line));
				}

				if (!cue.lines.empty())
					cues.push_back(std::move(cue));
			}

			return cues;
		}

		std::vector<std::string> parseAssText(std::string_view text)
		{
			std::string plain = removeTags(text, '{', '}');
			std::vector<std::string> lines;
			std::string current;

			for (size_t i = 0; i < plain.size(); ++i)
			{
				if (plain[i] == '\\' && i + 1 < plain.size())
				{
					const char code = plain[i + 1];
					if (code == 'N' || code == 'n')
					{
						lines.push_back(std::move(current));
						current.clear();
						++i;
						continue;
					}

					if (code == 'h')
					{
						current.push_back(' ');
						++i;
						continue;
					}
				}

				current.push_back(plain[i]);
			}

			lines.push_back(std::move(current));
			lines.erase(std::remove_if(lines.begin(), lines.end(),
				[](const std::string& line) { return trimAscii(line).empty(); }), lines.end());

			return lines;
		}

		std::vector<SubtitleCue> parseAssCues(std::string_view text)
		{
			constexpr std::string_view formatPrefix = "Format:";
			constexpr std::string_view dialoguePrefix = "Dialogue:";

			std::vector<SubtitleCue> cues;
			bool inEvents = false;
			size_t startField = 1, endField = 2, textField = 9;

			for (std::string_view rawLine : splitLines(text))
			{
				const std::string_view line = trimAscii(rawLine);
				if (!line.empty() && line.front() == '[')
				{
					inEvents = line == "[Events]";
					continue;
				}

				if (!inEvents)
					continue;

				const bool isFormat = IO::startsWith(line, formatPrefix);
				if (!isFormat && !IO::startsWith(line, dialoguePrefix))
					continue;

				// The text field is always last and may itself contain commas
				const size_t maxFields = isFormat ? SIZE_MAX : textField + 1;
				std::string_view remaining = line.substr((isFormat ? formatPrefix : dialoguePrefix).size());
				std::vector<std::string_view> fields;
				while (fields.size() + 1 < maxFields)
				{
					const size_t comma = remaining.find(',');
					if (comma == std::string_view::npos)
						break;

					fields.push_back(trimAscii(remaining.substr(0, comma)));
					remaining = remaining.substr(comma + 1);
				}
				fields.push_back(isFormat ? trimAscii(remaining) : remaining);

				if (isFormat)
				{
					for (size_t i = 0; i < fields.size(); ++i)
					{
						if (fields[i] == "Start") startField = i;
						else if (fields[i] == "End") endField = i;
						else if (fields[i] == "Text") textField = i;
					}
					continue;
				}

				if (fields.size() <= textField || fields.size() <= startField || fields.size() <= endField)
					continue;

				// Vector drawings have no readable text, only path commands
				if (fields[textField].find("\\p1") != std::string_view::npos)
					continue;

				SubtitleCue cue{};
				if (!parseTimestamp(fields[startField], cue.start) || !parseTimestamp(fields[endField], cue.end))
					continue;

				cue.lines = parseAssText(fields[textField]);
				if (!cue.lines.empty())
					cues.push_back(std::move(cue));
			}

			return cues;
		}

		bool isValidUtf8(const std::vector<uint8_t>& bytes)
		{
			for (size_t i = 0; i < bytes.size(); ++i)
			{
				const uint8_t lead = bytes[i];
				size_t continuationBytes;
				if (lead < 0x80) continuationBytes = 0;
				else if ((lead >> 5) == 0x06) continuationBytes = 1;
				else if ((lead >> 4) == 0x0E) continuationBytes = 2;
				else if ((lead >> 3) == 0x1E) continuationBytes = 3;
				else return false;

				if (i + continuationBytes >= bytes.size())
					return false;

				for (size_t c = 0; c < continuationBytes; ++c)
				{
					if ((bytes[++i] & 0xC0) != 0x80)
						return false;
				}
			}

			return true;
		}

		std::string decodeUtf16(const std::vector<uint8_t>& bytes, bool bigEndian)
		{
			std::wstring wide;
			wide.reserve(bytes.size() / 2);
			for (size_t i = 2; i + 1 < bytes.size(); i += 2)
			{
				const uint8_t high = bytes[bigEndian ? i : i + 1];
				const uint8_t low = bytes[bigEndian ? i + 1 : i];
				wide.push_back(static_cast<wchar_t>((high << 8) | low));
			}

			return IO::wideStringToMb(wide);
		}

		// Older Japanese subtitle files are commonly Shift-JIS with no BOM to identify them
		std::string decodeShiftJis(const std::vector<uint8_t>& bytes)
		{
			const char* source = reinterpret_cast<const char*>(bytes.data());
			const int sourceSize = static_cast<int>(bytes.size());
			const int wideSize = MultiByteToWideChar(SHIFT_JIS_CODE_PAGE, 0, source, sourceSize, nullptr, 0);
			if (wideSize <= 0)
				return {};

			std::wstring wide(wideSize, L'\0');
			MultiByteToWideChar(SHIFT_JIS_CODE_PAGE, 0, source, sourceSize, wide.data(), wideSize);
			return IO::wideStringToMb(wide);
		}

		std::string decodeToUtf8(const std::vector<uint8_t>& bytes)
		{
			if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
				return decodeUtf16(bytes, false);

			if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
				return decodeUtf16(bytes, true);

			if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
				return std::string(bytes.begin() + 3, bytes.end());

			if (isValidUtf8(bytes))
				return std::string(bytes.begin(), bytes.end());

			return decodeShiftJis(bytes);
		}
	}

	std::vector<SubtitleCue> parseSubtitles(const std::vector<uint8_t>& fileBytes)
	{
		const std::string text = decodeToUtf8(fileBytes);
		std::vector<SubtitleCue> cues = text.find("[Events]") != std::string::npos ? parseAssCues(text) : parseTimedTextCues(text);

		std::stable_sort(cues.begin(), cues.end(),
			[](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });

		return cues;
	}

	void Subtitles::load(const std::string& newFilename)
	{
		filename = newFilename;
		cues.clear();

		if (filename.empty() || !IO::File::exists(filename))
			return;

		IO::File file(filename, IO::FileMode::ReadBinary);
		cues = parseSubtitles(file.readAllBytes());
	}

	std::vector<std::string> Subtitles::getLinesAt(double seconds) const
	{
		std::vector<std::string> result;
		for (const SubtitleCue& cue : cues)
		{
			if (cue.start > seconds)
				break;

			if (seconds >= cue.end)
				continue;

			// Styled ASS files often repeat a line on several layers
			for (const std::string& line : cue.lines)
			{
				if (std::find(result.begin(), result.end(), line) == result.end())
					result.push_back(line);
			}
		}

		return result;
	}
}
