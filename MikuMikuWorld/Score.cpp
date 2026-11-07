#include "Score.h"

namespace MikuMikuWorld
{
	std::string formatArtistCredits(const std::string& lyricist, const std::string& composer,
		const std::string& arranger, const std::string& vocalist)
	{
		return u8"作詞：" + lyricist + u8"　作曲：" + composer + u8"　編曲：" + arranger + u8"　Vo. " + vocalist;
	}

	bool parseArtistCredits(const std::string& artist, std::string& lyricist, std::string& composer,
		std::string& arranger, std::string& vocalist)
	{
		lyricist.clear();
		composer.clear();
		arranger.clear();
		vocalist.clear();

		static const std::string lyricistTag = u8"作詞：";
		static const std::string composerTag = u8"　作曲：";
		static const std::string arrangerTag = u8"　編曲：";
		static const std::string vocalistTag = u8"　Vo. ";

		if (artist.compare(0, lyricistTag.size(), lyricistTag) != 0)
			return false;

		size_t composerPos = artist.find(composerTag, lyricistTag.size());
		if (composerPos == std::string::npos)
			return false;

		size_t arrangerPos = artist.find(arrangerTag, composerPos + composerTag.size());
		if (arrangerPos == std::string::npos)
			return false;

		size_t vocalistPos = artist.find(vocalistTag, arrangerPos + arrangerTag.size());
		if (vocalistPos == std::string::npos)
			return false;

		lyricist = artist.substr(lyricistTag.size(), composerPos - lyricistTag.size());
		composer = artist.substr(composerPos + composerTag.size(), arrangerPos - (composerPos + composerTag.size()));
		arranger = artist.substr(arrangerPos + arrangerTag.size(), vocalistPos - (arrangerPos + arrangerTag.size()));
		vocalist = artist.substr(vocalistPos + vocalistTag.size());

		return true;
	}

	Score::Score()
	{
		metadata.title = "";
		metadata.author = "";
		metadata.lyricist = "";
		metadata.composer = "";
		metadata.arranger = "";
		metadata.vocalist = "";
		metadata.musicOffset = 0;

		tempoChanges.push_back(Tempo());
		timeSignatures[0] = { 0, 4, 4 };

		fever.startTick = fever.endTick = -1;
	}
}
