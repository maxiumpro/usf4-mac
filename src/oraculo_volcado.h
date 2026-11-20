#pragma once
// Volcado del oraculo y hash de estado, copiados de SF4 Ember Netplay (MIT,
// https://github.com/Confetti3/SF4-Ember-Netplay; avisos en su LICENSE y
// ATTRIBUTION.md), rama local `oraculo` del arbol de W0:
//   src/common/OracleDump.hxx       formato del CSV (Row, HeaderLine, FormatRow, Dump)
//   src/common/sf4e__StateHash.hxx  Hasher (FNV-1a/64, codificacion canonica)
// Sin cambios de logica: solo el espacio de nombres (usf4::oraculo) y que el
// Hasher no trae la parte de _controlfp (el `# fp:` lo da oraculo.cpp desde el
// x87 y el MXCSR del invitado). El comentario original de OracleDump.hxx sigue:
//


// Oracle dump (usf4-recomp, phase W0): one CSV row per simulated frame of a
// replay, so two runs of the same replay (or the original game and a port of
// it) can be compared frame by frame. Off unless SF4E_ORACLE_DUMP names a
// file. Nothing here touches the game; it only formats and writes values the
// caller read. Standard library only, so it compiles and tests without the
// game's dependencies.
//
// Columns (header line "seq,round,cursor,..."):
//   seq          0-based row number in this file
//   round        recorder round being played
//   cursor       recorder cursor (the frame just played is cursor - 1)
//   engine_frame GetNumFramesSimulated integral, as a signed 16-bit value
//   flow,substate  CurrentBattleFlow, CurrentBattleFlowSubstate
//   overall,flow_hash,p1_hash,p2_hash  Ember's v2 semantic hashes (hex, 16 digits)
//   rng          EmRandom seed at System+0x54 (NOT part of Ember's hashes)
//   script_frame index of the scripted-fight frame whose input was just played (training
//                oracle run), -1 when the row comes from a replay
//   in_p1,in_p2  the raw input word each side was given on that frame (scripted run; 0 else)
//   p1_<field>,p2_<field>  the raw 32-bit words behind p1_hash and p2_hash
//   extra columns (W0b, only when SF4E_ORACLE_EXTRA is set), after the p2_ words: boxes, projectiles
//                (see sf4e__Oracle.cxx, ExtraNames); a file without them has the old header.
//
// Lines starting with '#' are comments (provenance, floating-point mode).
//
// Original work for the usf4-recomp oracle; built on SF4 Ember Netplay (MIT).

#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

namespace usf4 { namespace oraculo {

// ---- Hasher (sf4e__StateHash.hxx) ----
struct Hasher {
	std::uint64_t h;

	Hasher() : h(14695981039346656037ULL) {}

	void U8(std::uint8_t v) {
		h ^= (std::uint64_t)v;
		h *= 1099511628211ULL;
	}
	void U16(std::uint16_t v) {
		U8((std::uint8_t)(v & 0xff));
		U8((std::uint8_t)(v >> 8));
	}
	void U32(std::uint32_t v) {
		U8((std::uint8_t)(v & 0xff));
		U8((std::uint8_t)((v >> 8) & 0xff));
		U8((std::uint8_t)((v >> 16) & 0xff));
		U8((std::uint8_t)((v >> 24) & 0xff));
	}
	void U64(std::uint64_t v) {
		U32((std::uint32_t)(v & 0xffffffffULL));
		U32((std::uint32_t)(v >> 32));
	}
	void I16(std::int16_t v) { U16((std::uint16_t)v); }
	void I32(std::int32_t v) { U32((std::uint32_t)v); }
	// Floats participate as their exact bit pattern. Deterministic
	// simulation must produce bit-identical floats; encoding the pattern
	// (not a rounded value) keeps that property observable.
	void F32(float v) {
		std::uint32_t bits;
		memcpy(&bits, &v, sizeof(bits));
		U32(bits);
	}
	// A fixed-point value with explicit component widths.
	void Fixed(std::uint16_t fractional, std::int16_t integral) {
		U16(fractional);
		I16(integral);
	}

	std::uint64_t Value() const { return h; }
};

// ---- Fila y escritor (OracleDump.hxx) ----
struct Row {
	int round = 0;
	std::uint32_t cursor = 0;
	std::int16_t engineFrame = 0;
	std::uint32_t flow = 0, substate = 0;
	std::uint64_t overall = 0, flowHash = 0, chara[2] = {0, 0};
	std::uint32_t rng = 0;
	int scriptFrame = -1;
	std::uint32_t input[2] = {0, 0};
	std::vector<std::uint32_t> words[2];
	std::vector<std::uint32_t> extra;   // W0b columns after the p2_ words, in ExtraNames order (empty: none)
};

inline std::string Hex64(std::uint64_t v) { char t[24]; std::snprintf(t, sizeof t, "%016llx", static_cast<unsigned long long>(v)); return t; }
inline std::string Hex32(std::uint32_t v) { char t[16]; std::snprintf(t, sizeof t, "%08x", v); return t; }

inline std::string HeaderLine(const std::vector<std::string>& fieldNames, const std::vector<std::string>& extraNames = {}) {
	std::string line = "seq,round,cursor,engine_frame,flow,substate,overall,flow_hash,p1_hash,p2_hash,rng,script_frame,in_p1,in_p2";
	for (int side = 0; side < 2; side++)
		for (const std::string& name : fieldNames) line += std::string(",") + (side ? "p2_" : "p1_") + name;
	for (const std::string& name : extraNames) line += "," + name;
	return line;
}

inline std::string FormatRow(std::uint64_t seq, const Row& r) {
	std::string line = std::to_string(seq) + "," + std::to_string(r.round) + "," + std::to_string(r.cursor) + "," +
		std::to_string(r.engineFrame) + "," + std::to_string(r.flow) + "," + std::to_string(r.substate) + "," +
		Hex64(r.overall) + "," + Hex64(r.flowHash) + "," + Hex64(r.chara[0]) + "," + Hex64(r.chara[1]) + "," + Hex32(r.rng) + "," + std::to_string(r.scriptFrame) + "," + Hex32(r.input[0]) + "," + Hex32(r.input[1]);
	for (int side = 0; side < 2; side++)
		for (std::uint32_t w : r.words[side]) line += "," + Hex32(w);
	for (std::uint32_t w : r.extra) line += "," + Hex32(w);
	return line;
}

// The same state twice in a row is one frame held (pause, replay controls),
// not a new frame.
inline bool SameFrame(const Row& a, const Row& b) {
	return a.round == b.round && a.cursor == b.cursor && a.engineFrame == b.engineFrame && a.flow == b.flow &&
		a.substate == b.substate && a.overall == b.overall && a.rng == b.rng && a.scriptFrame == b.scriptFrame;
}

class Dump {
public:
	~Dump() { Close(); }
	// Truncates the file and writes the comment lines and the header.
	bool Open(const std::string& path, const std::vector<std::string>& comments, const std::vector<std::string>& fieldNames, const std::vector<std::string>& extraNames = {}) {
		Close();
#ifdef _WIN32
		if (fopen_s(&file_, path.c_str(), "wb")) file_ = nullptr;
#else
		file_ = std::fopen(path.c_str(), "wb");
#endif
		if (!file_) return false;
		for (const std::string& c : comments) std::fprintf(file_, "# %s\n", c.c_str());
		std::fprintf(file_, "%s\n", HeaderLine(fieldNames, extraNames).c_str());
		std::fflush(file_);
		rows_ = 0; haveLast_ = false;
		return true;
	}
	bool IsOpen() const { return file_ != nullptr; }
	// False when the row repeats the last one (nothing is written).
	bool Append(const Row& row) {
		if (!file_) return false;
		if (haveLast_ && SameFrame(last_, row)) return false;
		std::fprintf(file_, "%s\n", FormatRow(rows_, row).c_str());
		// Flushed so a game that is ended early still leaves its rows.
		std::fflush(file_);
		last_ = row; haveLast_ = true; ++rows_;
		return true;
	}
	std::uint64_t Rows() const { return rows_; }
	void Close() { if (file_) { std::fclose(file_); file_ = nullptr; } }
private:
	std::FILE* file_ = nullptr;
	std::uint64_t rows_ = 0;
	Row last_;
	bool haveLast_ = false;
};

} }
