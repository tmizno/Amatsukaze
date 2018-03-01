/**
* Amtasukaze �j�R�j�R���� ASS generator
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <regex>
#include <array>

#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "ProcessThread.hpp"


class NicoJK : public AMTObject
{
public:
	NicoJK(AMTContext& ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
	{	}

	bool makeASS(int serviceId, time_t startTime, int duration) 
	{
		Stopwatch sw;
		sw.start();
		if (setting_.isNicoJK18Enabled()) {
			getJKNum(serviceId);
			if (jknum_ == -1) return false;

			// �擾������\��
			tm t;
			if (gmtime_s(&t, &startTime) != 0) {
				THROW(RuntimeException, "gmtime_s failed ...");
			}
			t.tm_hour += 9; // GMT+9
			mktime(&t);
			ctx.info("%s (jk%d) %d�N%02d��%02d�� %02d��%02d��%02d�b ���� %d����%02d��%02d�b",
				tvname_.c_str(), jknum_,
				t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
				duration / 3600, (duration / 60) % 60, duration % 60);

			if (!getNicoJKXml(startTime, duration)) return false;
			ctx.info("�R�����gXML�擾: %.2f�b", sw.getAndReset());
			if (!nicoConvASS(false, startTime)) return false;
			ctx.info("�R�����gASS����: %.2f�b", sw.getAndReset());
		}
		else {
			if (!nicoConvASS(true, startTime)) return false;
			ctx.info("�R�����gASS����: %.2f�b", sw.getAndReset());
		}
		readASS();
		ctx.info("�R�����gASS�ǂݍ���: %.2f�b", sw.getAndReset());
		return true;
	}

	bool isFail() const {
		return isFail_;
	}

	const std::array<std::vector<std::string>, NICOJK_MAX>& getHeaderLines() const {
		return headerlines_;
	}

	const std::array<std::vector<NicoJKLine>, NICOJK_MAX>& getDialogues() const {
		return dislogues_;
	}

private:
	class MySubProcess : public EventBaseSubProcess
	{
	public:
		MySubProcess(const std::string& args)
			: EventBaseSubProcess(args)
			, outConv(false)
			, errConv(true)
		{ }

		~MySubProcess() {
			outConv.Flush();
			errConv.Flush();
		}

		bool isFail() const {
			// �Ή��`�����l�����Ȃ��ꍇ�̓G���[�ɂ������Ȃ�
			// ��������Ȃ��ꍇ�́A�Ή�����`�����l�����Ȃ��Ɣ��f����
			// ���ɂ��܂����ʂ�����@��������Ȃ�
			return errConv.nlines > 0;
		}

	private:
		class OutConv : public UTF8Converter
		{
		public:
			OutConv(bool isErr) : nlines(0), isErr(isErr) { }
			int nlines;
		protected:
			bool isErr;
			virtual void OnTextLine(const std::vector<char>& line) {
				auto out = isErr ? stderr : stdout;
				fwrite(line.data(), line.size(), 1, out);
				fprintf(out, "\n");
				fflush(out);
				++nlines;
			}
		};

		OutConv outConv, errConv;
		virtual void onOut(bool isErr, MemoryChunk mc) {
			(isErr ? errConv : outConv).AddBytes(mc);
		}
	};

	const ConfigWrapper& setting_;

	bool isFail_;
	std::array<std::vector<std::string>, NICOJK_MAX> headerlines_;
	std::array<std::vector<NicoJKLine>, NICOJK_MAX> dislogues_;

	int jknum_;
	std::string tvname_;

	void getJKNum(int serviceId)
	{
		File file(setting_.getNicoConvChSidPath(), "r");
		std::regex re("([^\\t]+)\\t([^\\t]+)\\t([^\\t]+)\\t([^\\t]+)\\t([^\\t]+)");
		std::string str;
		while (file.getline(str)) {
			std::smatch m;
			if (std::regex_search(str, m, re)) {
				int jknum = strtol(m[1].str().c_str(), nullptr, 0);
				int sid = strtol(m[3].str().c_str(), nullptr, 0);
				if (sid == serviceId) {
					jknum_ = jknum;
					tvname_ = m[5].str();
					return;
				}
			}
		}
		jknum_ = -1;
	}

	std::string MakeNicoJK18Args(int jknum, size_t startTime, size_t endTime)
	{
		return StringFormat("\"%s\" jk%d %zu %zu -x -f \"%s\"",
			GetModuleDirectory() + "\\NicoJK18Client.exe",
			jknum, startTime, endTime,
			setting_.getTmpNicoJKXMLPath());
	}

	bool getNicoJKXml(time_t startTime, int duration)
	{
		auto args = MakeNicoJK18Args(jknum_, (size_t)startTime, (size_t)startTime + duration);
		ctx.info(args.c_str());
		StdRedirectedSubProcess process(args);
		int exitCode = process.join();
		if (exitCode == 0 && File::exists(setting_.getTmpNicoJKXMLPath())) {
			return true;
		}
		isFail_ = true;
		return false;
	}

	enum NicoJKMask {
		MASK_720S = 1,
		MASK_720T = 2,
		MASK_1080S = 4,
		MASK_1080T = 8,
		MASK_720X = MASK_720S | MASK_720T,
		MASK_1080X = MASK_1080S | MASK_1080T,
	};

	void makeT(NicoJKType srcType, NicoJKType dstType)
	{
		File file(setting_.getTmpNicoJKASSPath(srcType), "r");
		File dst(setting_.getTmpNicoJKASSPath(dstType), "w");
		std::string str;
		while (file.getline(str)) {
			dst.writeline(str);
			if (str == "[V4+ Styles]") break;
		}

		// Format ...
		file.getline(str);
		dst.writeline(str);

		while (file.getline(str)) {
			if (str.size() >= 6 && str.substr(0, 6) == "Style:") {
				// �ύX�O
				//|0           |1         |2 |3         |4         |5         |6         |7 |8|9|0|1  |2  |3|4   |5|6|7|8|9 |0 |1 |2|
				// Style: white,MS PGothic,28,&H00ffffff,&H00ffffff,&H00000000,&H00000000,-1,0,0,0,200,200,0,0.00,1,0,4,7,20,20,40,1
				// �ύX��
				// Style: white,MS PGothic,28,&H70ffffff,&H70ffffff,&H70000000,&H70000000,-1,0,0,0,200,200,0,0.00,1,1,0,7,20,20,40,1
				auto tokens = split(str, ",");
				for (int i = 3; i < 7; ++i) {
					// �����x
					tokens[i][2] = '7';
					tokens[i][3] = '0';
				}
				tokens[16] = "1"; // Outline����
				tokens[17] = "0"; // Shadow�Ȃ�

				StringBuilder sb;
				for (int i = 0; i < (int)tokens.size(); ++i) {
					sb.append("%s%s", i ? "," : "", tokens[i]);
				}
				dst.writeline(sb.str());
			}
			else {
				dst.writeline(str);
				break;
			}
		}

		// �c��
		while (file.getline(str)) dst.writeline(str);
	}

	std::string MakeNicoConvASSArgs(bool isTS, size_t startTime, NicoJKType type)
	{
		int width[] = { 1280, 1280, 1920, 1920 };
		int height[] = { 720, 720, 1080, 1080 };
		StringBuilder sb;
		sb.append("\"%s\" -width %d -height %d -wfilename \"%s\" -chapter 0",
			setting_.getNicoConvAssPath(),
			width[(int)type], height[(int)type],
			setting_.getTmpNicoJKASSPath(type));
		if (isTS == false) {
			sb.append(" -tx_starttime %zu", startTime);
		}
		sb.append(" \"%s\"", isTS ? setting_.getSrcFilePath() : setting_.getTmpNicoJKXMLPath());
		return sb.str();
	}

	bool nicoConvASS(bool isTS, size_t startTime)
	{
		NicoJKMask mask_i[] = { MASK_720X , MASK_1080X };
		NicoJKType type_s[] = { NICOJK_720S , NICOJK_1080S };
		NicoJKMask mask_t[] = { MASK_720T , MASK_1080T };
		NicoJKType type_t[] = { NICOJK_720T , NICOJK_1080T };

		int typemask = setting_.getNicoJKMask();
		for (int i = 0; i < 2; ++i) {
			if (mask_i[i] & typemask) {
				auto args = MakeNicoConvASSArgs(isTS, startTime, type_s[i]);
				ctx.info(args.c_str());
				MySubProcess process(args);
				int exitCode = process.join();
				if (exitCode == 0 && File::exists(setting_.getTmpNicoJKASSPath(type_s[i]))) {
					if (mask_t[i] & typemask) {
						makeT(type_s[i], type_t[i]);
					}
					continue;
				}
				isFail_ = process.isFail();
				return false;
			}
		}

		return true;
	}

	static double toClock(int h, int m, int s, int ss) {
		return (((((h * 60.0) + m) * 60.0) + s) * 100.0 + ss) * 900.0;
	}

	void readASS()
	{
		int typemask = setting_.getNicoJKMask();
		for (int i = 0; i < NICOJK_MAX; ++i) {
			if ((1 << i) & typemask) {
				File file(setting_.getTmpNicoJKASSPath((NicoJKType)i), "r");
				std::string str;
				while (file.getline(str)) {
					headerlines_[i].push_back(str);
					if (str == "[Events]") break;
				}

				// Format ...
				file.getline(str);
				headerlines_[i].push_back(str);

				std::regex re("Dialogue: 0,(\\d):(\\d\\d):(\\d\\d)\\.(\\d\\d),(\\d):(\\d\\d):(\\d\\d)\\.(\\d\\d)(.*)");

				while (file.getline(str)) {
					std::smatch m;
					if (std::regex_search(str, m, re)) {
						NicoJKLine elem = {
							toClock(
								std::stoi(m[1].str()), std::stoi(m[2].str()),
								std::stoi(m[3].str()), std::stoi(m[4].str())),
							toClock(
								std::stoi(m[5].str()), std::stoi(m[6].str()),
								std::stoi(m[7].str()), std::stoi(m[8].str())),
							m[9].str()
						};
						dislogues_[i].push_back(elem);
					}
				}
			}
		}
	}
};

class NicoJKFormatter : public AMTObject
{
public:
	NicoJKFormatter(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

	std::string generate(
		const std::vector<std::string>& headers,
		const std::vector<NicoJKLine>& dialogues)
	{
		sb.clear();
		for (auto& header : headers) {
			sb.append("%s\n", header);
		}
		for (auto& dialogue : dialogues) {
			sb.append("Dialogue: 0,");
			time(dialogue.start);
			sb.append(",");
			time(dialogue.end);
			sb.append("%s\n", dialogue.line);
		}
		return sb.str();
	}

private:
	StringBuilder sb;

	void time(double t) {
		double totalSec = t / MPEG_CLOCK_HZ;
		double totalMin = totalSec / 60;
		int h = (int)(totalMin / 60);
		int m = (int)totalMin % 60;
		double sec = totalSec - (int)totalMin * 60;
		sb.append("%d:%02d:%05.2f", h, m, sec);
	}
};