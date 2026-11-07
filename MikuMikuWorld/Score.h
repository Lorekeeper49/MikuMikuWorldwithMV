#pragma once
#include "Note.h"
#include "Tempo.h"
#include <string>
#include <map>
#include <vector>

namespace MikuMikuWorld
{
	extern int nextSkillID;
	extern int nextCheerID;

	struct SkillTrigger
	{
		int ID;
		int tick;
	};

	struct CheerTrigger
	{
		int ID;
		int tick;
	};

	struct Fever
	{
		int startTick;
		int endTick;
	};

	struct HiSpeedChange
	{
		int tick;
		float speed;
	};

	struct ScoreMetadata
	{
		std::string title;
		std::string lyricist;
		std::string composer;
		std::string arranger;
		std::string vocalist;
		std::string author;
		std::string musicFile;
		std::string jacketFile;
		std::string subtitleFile;
		float musicOffset;
		std::string musicVideoFile;
		float musicVideoOffset{};
	};

	// Composes the SUS #ARTIST value from the individual credit fields, e.g.
	// "作詞：A　作曲：B　編曲：C　Vo. D"
	std::string formatArtistCredits(const std::string& lyricist, const std::string& composer,
		const std::string& arranger, const std::string& vocalist);

	// Parses an SUS #ARTIST value into the individual credit fields. If the value does not
	// match the expected "作詞：…　作曲：…　編曲：…　Vo. …" format, all fields are cleared
	// and false is returned.
	bool parseArtistCredits(const std::string& artist, std::string& lyricist, std::string& composer,
		std::string& arranger, std::string& vocalist);

	struct Score
	{
		ScoreMetadata metadata;
		std::map<int, Note> notes;
		std::map<int, HoldNote> holdNotes;
		std::vector<Tempo> tempoChanges;
		std::map<int, TimeSignature> timeSignatures;
		std::vector<HiSpeedChange> hiSpeedChanges;
		std::vector<SkillTrigger> skills;
		std::vector<CheerTrigger> cheers;
		Fever fever;

		Score();
	};
}