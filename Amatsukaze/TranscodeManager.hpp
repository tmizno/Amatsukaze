/**
* Transcode manager
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <memory>

#include "StreamUtils.hpp"
#include "TsSplitter.hpp"
#include "Transcode.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"

// �J���[�X�y�[�X��`���g������
#include "libavutil/pixfmt.h"

namespace av {

// �J���[�X�y�[�X3�Z�b�g
// x265�͐��l���̂܂܂ł�OK�����Ax264��help���������string�łȂ����
// �Ȃ�Ȃ��悤�Ȃ̂ŕϊ����`
// �Ƃ肠����ARIB STD-B32 v3.7�ɏ����Ă���̂���

// 3���F
static const char* getColorPrimStr(int color_prim) {
	switch (color_prim) {
	case AVCOL_PRI_BT709: return "bt709";
	case AVCOL_PRI_BT2020: return "bt2020";
	default:
		THROWF(FormatException,
			"Unsupported color primaries (%d)", color_prim);
	}
	return NULL;
}

// �K���}
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
	switch (transfer_characteritics) {
	case AVCOL_TRC_BT709: return "bt709";
	case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
	case AVCOL_TRC_BT2020_10: return "bt2020-10";
	case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
	case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
	default:
		THROWF(FormatException,
			"Unsupported color transfer characteritics (%d)", transfer_characteritics);
	}
	return NULL;
}

// �ϊ��W��
static const char* getColorSpaceStr(int color_space) {
	switch (color_space) {
	case AVCOL_SPC_BT709: return "bt709";
	case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
	default:
		THROWF(FormatException,
			"Unsupported color color space (%d)", color_space);
	}
	return NULL;
}

} // namespace av {

enum ENUM_ENCODER {
	ENCODER_X264,
	ENCODER_X265,
	ENCODER_QSVENC,
};

struct BitrateSetting {
  double a, b;
  double h264;
  double h265;

  double getTargetBitrate(VIDEO_STREAM_FORMAT format, double srcBitrate) const {
    double base = a * srcBitrate + b;
    if (format == VS_H264) {
      return base * h264;
    }
    else if (format == VS_H265) {
      return base * h265;
    }
    return base;
  }
};

static const char* encoderToString(ENUM_ENCODER encoder) {
	switch (encoder) {
	case ENCODER_X264: return "x264";
	case ENCODER_X265: return "x265";
	case ENCODER_QSVENC: return "QSVEnc";
	}
	return "Unknown";
}

static std::string makeEncoderArgs(
	ENUM_ENCODER encoder,
	const std::string& binpath,
	const std::string& options,
	const VideoFormat& fmt,
	const std::string& outpath)
{
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";

	// y4m�w�b�_�ɂ���̂ŕK�v�Ȃ�
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);
	}

	// �C���^�[���[�X
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
		ss << (fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		ss << (fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	ss << " " << options << " -o \"" << outpath << "\"";

	// ���͌`��
	switch (encoder) {
	case ENCODER_X264:
		ss << " --demuxer y4m -";
		break;
	case ENCODER_X265:
		ss << " --y4m --input -";
		break;
	case ENCODER_QSVENC:
		ss << " --y4m -i -";
		break;
	}
	
	return ss.str();
}

static std::string makeMuxerArgs(
	const std::string& binpath,
	const std::string& inVideo,
	const std::vector<std::string>& inAudios,
	const std::string& outpath)
{
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";
	ss << " -i \"" << inVideo << "\"";
	for (const auto& inAudio : inAudios) {
		ss << " -i \"" << inAudio << "\"";
	}
	ss << " -o \"" << outpath << "\"";

	return ss.str();
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath)
{
	std::ostringstream ss;
	ss << "\"" << binpath << "\"";
	ss << " --track 1";
	ss << " --timecode \"" << timecodepath << "\"";
	ss << " \"" << inpath << "\"";
	ss << " \"" << outpath << "\"";
	return ss.str();
}

struct TranscoderSetting {
	// ���̓t�@�C���p�X�i�g���q���܂ށj
	std::string tsFilePath;
	// �o�̓t�@�C���p�X�i�g���q�������j
	std::string outVideoPath;
	// ���ԃt�@�C���v���t�B�b�N�X
	std::string intFileBasePath;
	// �ꎞ�����t�@�C���p�X�i�g���q���܂ށj
	std::string audioFilePath;
	// ���ʏ��JSON�o�̓p�X
	std::string outInfoJsonPath;
	// �G���R�[�_�ݒ�
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
  std::string timelineditorPath;
  bool twoPass;
  bool autoBitrate;
  BitrateSetting bitrate;
	int serviceId;
	// �f�o�b�O�p�ݒ�
	bool dumpStreamInfo;

	std::string getIntVideoFilePath(int index) const
	{
		std::ostringstream ss;
		ss << intFileBasePath << "-" << index << ".mpg";
		return ss.str();
	}

	std::string getStreamInfoPath() const
	{
		return intFileBasePath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << intFileBasePath << "-" << vindex << "-" << index << ".raw";
		return ss.str();
  }

  std::string getEncStatsFilePath(int vindex, int index) const
  {
    std::ostringstream ss;
    ss << intFileBasePath << "-" << vindex << "-" << index << "-stats.log";
    return ss.str();
  }

	std::string getEnvTimecodeFilePath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << intFileBasePath << "-" << vindex << "-" << index << "-tc.txt";
		return ss.str();
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex) const
	{
		std::ostringstream ss;
		ss << intFileBasePath << "-" << vindex << "-" << index << "-" << aindex << ".aac";
		return ss.str();
	}

	std::string getVfrTmpFilePath(int index) const
	{
		std::ostringstream ss;
		ss << intFileBasePath << "-" << index << ".mp4";
		return ss.str();
	}

	std::string getOutFilePath(int index) const
	{
		std::ostringstream ss;
		ss << outVideoPath;
		if (index != 0) {
			ss << "-" << index;
		}
		ss << ".mp4";
		return ss.str();
	}

	void dump(AMTContext& ctx) const {
		ctx.info("[�ݒ�]");
		ctx.info("Input: %s", tsFilePath.c_str());
		ctx.info("Output: %s", outVideoPath.c_str());
		ctx.info("IntVideo: %s", intFileBasePath.c_str());
		ctx.info("IntAudio: %s", audioFilePath.c_str());
		ctx.info("OutJson: %s", outInfoJsonPath.c_str());
		ctx.info("Encoder: %s", encoderToString(encoder));
		ctx.info("EncoderPath: %s", encoderPath.c_str());
		ctx.info("EncoderOptions: %s", encoderOptions.c_str());
		ctx.info("MuxerPath: %s", muxerPath.c_str());
    ctx.info("TimelineeditorPath: %s", timelineditorPath.c_str());
    ctx.info("autoBitrate: %d", autoBitrate);
    ctx.info("Bitrate: %f:%f:%f", bitrate.a, bitrate.b, bitrate.h264);
    ctx.info("twoPass: %d", twoPass);
		if (serviceId > 0) {
			ctx.info("ServiceId: 0x%04x", serviceId);
		}
		else {
			ctx.info("ServiceId: �w��Ȃ�");
		}
		ctx.info("DumpStreamInfo: %d", dumpStreamInfo);
	}
};


class AMTSplitter : public TsSplitter {
public:
	AMTSplitter(AMTContext& ctx, const TranscoderSetting& setting)
		: TsSplitter(ctx)
		, setting_(setting)
		, psWriter(ctx)
		, writeHandler(*this)
		, audioFile_(setting.audioFilePath, "wb")
		, videoFileCount_(0)
		, videoStreamType_(-1)
		, audioStreamType_(-1)
		, audioFileSize_(0)
		, srcFileSize_(0)
	{
		psWriter.setHandler(&writeHandler);
	}

	StreamReformInfo split()
	{
		writeHandler.resetSize();

		readAll();

		// for debug
		printInteraceCount();

		return StreamReformInfo(ctx, videoFileCount_,
			videoFrameList_, audioFrameList_, streamEventList_);
	}

	int64_t getSrcFileSize() const {
		return srcFileSize_;
	}

	int64_t getTotalIntVideoSize() const {
		return writeHandler.getTotalSize();
	}

protected:
	class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
		TsSplitter& this_;
		std::unique_ptr<File> file_;
		int64_t totalIntVideoSize_;
	public:
		StreamFileWriteHandler(TsSplitter& this_)
			: this_(this_), totalIntVideoSize_() { }
		virtual void onStreamData(MemoryChunk mc) {
			if (file_ != NULL) {
				file_->write(mc);
				totalIntVideoSize_ += mc.length;
			}
		}
		void open(const std::string& path) {
			file_ = std::unique_ptr<File>(new File(path, "wb"));
		}
		void close() {
			file_ = nullptr;
		}
		void resetSize() {
			totalIntVideoSize_ = 0;
		}
		int64_t getTotalSize() const {
			return totalIntVideoSize_;
		}
	};

	const TranscoderSetting& setting_;
	PsStreamWriter psWriter;
	StreamFileWriteHandler writeHandler;
	File audioFile_;

	int videoFileCount_;
	int videoStreamType_;
	int audioStreamType_;
	int64_t audioFileSize_;
	int64_t srcFileSize_;

	// �f�[�^
  std::vector<VideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;

	void readAll() {
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.tsFilePath, "rb");
		srcFileSize_ = srcfile.size();
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
		} while (readBytes == buffer.length);
	}

	static bool CheckPullDown(PICTURE_TYPE p0, PICTURE_TYPE p1) {
		switch (p0) {
		case PIC_TFF:
		case PIC_BFF_RFF:
			return (p1 == PIC_TFF || p1 == PIC_TFF_RFF);
		case PIC_BFF:
		case PIC_TFF_RFF:
			return (p1 == PIC_BFF || p1 == PIC_BFF_RFF);
		default: // ����ȊO�̓`�F�b�N�ΏۊO
			return true;
		}
	}

	void printInteraceCount() {

		if (videoFrameList_.size() == 0) {
			ctx.error("�t���[��������܂���");
			return;
		}

		// ���b�v�A���E���h���Ȃ�PTS�𐶐�
		std::vector<std::pair<int64_t, int>> modifiedPTS;
		int64_t videoBasePTS = videoFrameList_[0].PTS;
		int64_t prevPTS = videoFrameList_[0].PTS;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			int64_t PTS = videoFrameList_[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS.emplace_back(modPTS, i);
			prevPTS = modPTS;
		}

		// PTS�Ń\�[�g
		std::sort(modifiedPTS.begin(), modifiedPTS.end());

		// �t���[�����X�g���o��
		FILE* framesfp = fopen("frames.txt", "w");
		fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
		for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
			int64_t PTS = modifiedPTS[i].first;
			int decodeIndex = modifiedPTS[i].second;
			const VideoFrameInfo& frame = videoFrameList_[decodeIndex];
			int PTSdiff = -1;
			if (i < (int)modifiedPTS.size() - 1) {
				int64_t nextPTS = modifiedPTS[i + 1].first;
				const VideoFrameInfo& nextFrame = videoFrameList_[modifiedPTS[i + 1].second];
				PTSdiff = int(nextPTS - PTS);
				if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
					ctx.warn("Flag Check Error: PTS=%lld %s -> %s",
						PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
				}
			}
			fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
				i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
		}
		fclose(framesfp);

		// PTS�Ԋu���o��
		struct Integer {
			int v;
			Integer() : v(0) { }
		};

		std::array<int, MAX_PIC_TYPE> interaceCounter = { 0 };
		std::map<int, Integer> PTSdiffMap;
		prevPTS = -1;
		for (const auto& ptsIndex : modifiedPTS) {
			int64_t PTS = ptsIndex.first;
			const VideoFrameInfo& frame = videoFrameList_[ptsIndex.second];
			interaceCounter[(int)frame.pic]++;
			if (prevPTS != -1) {
				int PTSdiff = int(PTS - prevPTS);
				PTSdiffMap[PTSdiff].v++;
			}
			prevPTS = PTS;
		}

		ctx.info("[�f���t���[�����v���]");

		int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
		ctx.info("����: %f �b", totalTime / 90000.0);

		ctx.info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
			interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

		for (const auto& pair : PTSdiffMap) {
			ctx.info("(PTS_Diff,Cnt)=(%d,%d)", pair.first, pair.second.v);
		}
	}

	// TsSplitter���z�֐� //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		for (const VideoFrameInfo& frame : frames) {
      videoFrameList_.push_back(frame);
		}
		psWriter.outVideoPesPacket(clock, frames, packet);
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) {
		ctx.debug("[�f���t�H�[�}�b�g�ύX]");
		if (fmt.fixedFrameRate) {
			ctx.debug("�T�C�Y: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);
		}
		else {
			ctx.debug("�T�C�Y: %dx%d FPS: VFR", fmt.width, fmt.height);
		}

		// �o�̓t�@�C����ύX
		writeHandler.open(setting_.getIntVideoFilePath(videoFileCount_++));
		psWriter.outHeader(videoStreamType_, audioStreamType_);

		StreamEvent ev = StreamEvent();
		ev.type = VIDEO_FORMAT_CHANGED;
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}

	virtual void onAudioPesPacket(
		int audioIdx, 
		int64_t clock, 
		const std::vector<AudioFrameData>& frames, 
		PESPacket packet)
	{
		for (const AudioFrameData& frame : frames) {
			FileAudioFrameInfo info = frame;
			info.audioIdx = audioIdx;
			info.codedDataSize = frame.codedDataSize;
			info.fileOffset = audioFileSize_;
			audioFile_.write(MemoryChunk(frame.codedData, frame.codedDataSize));
			audioFileSize_ += frame.codedDataSize;
			audioFrameList_.push_back(info);
		}
		if (videoFileCount_ > 0) {
			psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);
		}
	}

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx.debug("[����%d�t�H�[�}�b�g�ύX]", audioIdx);
		ctx.debug("�`�����l��: %s �T���v�����[�g: %d",
			getAudioChannelString(fmt.channels), fmt.sampleRate);

		StreamEvent ev = StreamEvent();
		ev.type = AUDIO_FORMAT_CHANGED;
		ev.audioIdx = audioIdx;
		ev.frameIdx = (int)audioFrameList_.size();
		streamEventList_.push_back(ev);
	}

	// TsPacketSelectorHandler���z�֐� //

	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) {
		// �x�[�X�N���X�̏���
		TsSplitter::onPidTableChanged(video, audio);

		ASSERT(audio.size() > 0);
		videoStreamType_ = video.stype;
		audioStreamType_ = audio[0].stype;

		StreamEvent ev = StreamEvent();
		ev.type = PID_TABLE_CHANGED;
		ev.numAudio = (int)audio.size();
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}
};

class AMTVideoEncoder : public AMTObject {
public:
	AMTVideoEncoder(
		AMTContext&ctx,
		const TranscoderSetting& setting,
		StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, thread_(this, 8)
	{
		//
	}

	~AMTVideoEncoder() {
		delete[] encoders_; encoders_ = NULL;
	}

	void encode(int videoFileIndex) {
		videoFileIndex_ = videoFileIndex;

		int numEncoders = reformInfo_.getNumEncoders(videoFileIndex);
		if (numEncoders == 0) {
			ctx.warn("numEncoders == 0 ...");
			return;
		}

    // �r�b�g���[�g�v�Z
    VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
    int64_t srcBytes = 0, srcDuration = 0;
    for (int i = 0; i < numEncoders; ++i) {
      const auto& info = reformInfo_.getSrcVideoInfo(i, videoFileIndex);
      srcBytes += info.first;
      srcDuration += info.second;
    }
    double srcBitrate = ((double)srcBytes * 8 / 1000) / ((double)srcDuration / MPEG_CLOCK_HZ);
    ctx.info("���͉f���r�b�g���[�g: %d kbps", (int)srcBitrate);

    if (setting_.autoBitrate) {
      ctx.info("�ڕW�f���r�b�g���[�g: %d kbps",
        (int)setting_.bitrate.getTargetBitrate(srcFormat, srcBitrate));
    }

    auto getOptions = [&](int pass, int index) {
      std::ostringstream ss;
      ss << setting_.encoderOptions;
      if (setting_.autoBitrate) {
        double targetBitrate = setting_.bitrate.getTargetBitrate(srcFormat, srcBitrate);
        ss << " --bitrate " << (int)targetBitrate;
      }
      if (pass >= 0) {
        ss << " --pass " << pass;
        ss << " --stats \"" << setting_.getEncStatsFilePath(videoFileIndex_, index) << "\"";
      }
      return ss.str();
    };

    if (setting_.twoPass) {
      ctx.info("1/2�p�X �G���R�[�h�J�n");
      processAllData(getOptions, 1);
      ctx.info("2/2�p�X �G���R�[�h�J�n");
      processAllData(getOptions, 2);

      // ���ԃt�@�C���폜
      for (int i = 0; i < numEncoders; ++i) {
        remove(setting_.getEncStatsFilePath(videoFileIndex_, i).c_str());
      }
    }
    else {
      processAllData(getOptions, -1);
    }

		// ���ԃt�@�C���폜
    std::string intVideoFilePath = setting_.getIntVideoFilePath(videoFileIndex_);
		remove(intVideoFilePath.c_str());
	}

private:
	class SpVideoReader : public av::VideoReader {
	public:
		SpVideoReader(AMTVideoEncoder* this_)
			: VideoReader()
			, this_(this_)
		{ }
	protected:
		virtual void onFrameDecoded(av::Frame& frame) {
			this_->onFrameDecoded(frame);
		}
	private:
		AMTVideoEncoder* this_;
	};

	class SpDataPumpThread : public DataPumpThread<std::unique_ptr<av::Frame>> {
	public:
		SpDataPumpThread(AMTVideoEncoder* this_, int bufferingFrames)
			: DataPumpThread(bufferingFrames)
			, this_(this_)
		{ }
	protected:
		virtual void OnDataReceived(std::unique_ptr<av::Frame>&& data) {
			this_->onFrameReceived(std::move(data));
		}
	private:
		AMTVideoEncoder* this_;
	};

	const TranscoderSetting& setting_;
	StreamReformInfo& reformInfo_;

	int videoFileIndex_;
	av::EncodeWriter* encoders_;
	
	SpDataPumpThread thread_;

	std::unique_ptr<av::Frame> prevFrame_;

  void processAllData(std::function<std::string(int,int)> getOptions, int pass)
  {
    int numEncoders = reformInfo_.getNumEncoders(videoFileIndex_);
    const auto& format0 = reformInfo_.getFormat(0, videoFileIndex_);
    int bufsize = format0.videoFormat.width * format0.videoFormat.height * 3;

    // ������
    encoders_ = new av::EncodeWriter[numEncoders];
    SpVideoReader reader(this);

    for (int i = 0; i < numEncoders; ++i) {
      const auto& format = reformInfo_.getFormat(i, videoFileIndex_);
      std::string args = makeEncoderArgs(
        setting_.encoder,
        setting_.encoderPath,
        getOptions(pass, i),
        format.videoFormat,
        setting_.getEncVideoFilePath(videoFileIndex_, i));
      ctx.info("[�G���R�[�_�J�n]");
      ctx.info(args.c_str());
      encoders_[i].start(args, format.videoFormat, bufsize);
    }

    // �G���R�[�h�X���b�h�J�n
    thread_.start();

    // �G���R�[�h
    std::string intVideoFilePath = setting_.getIntVideoFilePath(videoFileIndex_);
    reader.readAll(intVideoFilePath);

    // �G���R�[�h�X���b�h���I�����Ď����Ɉ����p��
    thread_.join();

    // �c�����t���[��������
    for (int i = 0; i < numEncoders; ++i) {
      encoders_[i].finish();
    }

    // �I��
    prevFrame_ = nullptr;
    delete[] encoders_; encoders_ = NULL;
  }

	void onFrameDecoded(av::Frame& frame__) {
		// �t���[�����R�s�[���ăX���b�h�ɓn��
		thread_.put(std::unique_ptr<av::Frame>(new av::Frame(frame__)), 1);
	}

	void onFrameReceived(std::unique_ptr<av::Frame>&& frame) {

		// ffmpeg���ǂ�pts��wrap���邩������Ȃ��̂œ��̓f�[�^��
		// ����33bit�݂̂�����
		//�i26���Ԉȏ゠�铮�悾�Əd������\���͂��邪�����j
		int64_t pts = (*frame)()->pts & ((int64_t(1) << 33) - 1);

		int frameIndex = reformInfo_.getVideoFrameIndex(pts, videoFileIndex_);
		if (frameIndex == -1) {
			THROWF(FormatException, "Unknown PTS frame %lld", pts);
		}

		const VideoFrameInfo& info = reformInfo_.getVideoFrameInfo(frameIndex);
		auto& encoder = encoders_[reformInfo_.getEncoderIndex(frameIndex)];

		if (reformInfo_.isVFR()) {
			// VFR�̏ꍇ�͕K���P�������o��
			encoder.inputFrame(*frame);
		}
		else {
			// RFF�t���O����
			// PTS��inputFrame�ōĒ�`�����̂ŏC�����Ȃ��ł��̂܂ܓn��
			switch (info.pic) {
			case PIC_FRAME:
			case PIC_TFF:
			case PIC_TFF_RFF:
				encoder.inputFrame(*frame);
				break;
			case PIC_FRAME_DOUBLING:
				encoder.inputFrame(*frame);
				encoder.inputFrame(*frame);
				break;
			case PIC_FRAME_TRIPLING:
				encoder.inputFrame(*frame);
				encoder.inputFrame(*frame);
				encoder.inputFrame(*frame);
				break;
			case PIC_BFF:
				encoder.inputFrame(*makeFrameFromFields(
					(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
				break;
			case PIC_BFF_RFF:
				encoder.inputFrame(*makeFrameFromFields(
					(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
				encoder.inputFrame(*frame);
				break;
			}
		}

		reformInfo_.frameEncoded(frameIndex);
		prevFrame_ = std::move(frame);
	}

	static std::unique_ptr<av::Frame> makeFrameFromFields(av::Frame& topframe, av::Frame& bottomframe)
	{
		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

		AVFrame* top = topframe();
		AVFrame* bottom = bottomframe();
		AVFrame* dst = (*dstframe)();

		// �t���[���̃v���p�e�B���R�s�[
		av_frame_copy_props(dst, top);

		// �������T�C�Y�Ɋւ�������R�s�[
		dst->format = top->format;
		dst->width = top->width;
		dst->height = top->height;

		// �������m��
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		// ���g���R�s�[
		int bytesLumaLine;
		int bytesChromaLine;
		int chromaHeight;

		switch (dst->format) {
		case AV_PIX_FMT_YUV420P:
			bytesLumaLine = dst->width;
			bytesChromaLine = dst->width / 2;
			chromaHeight = dst->height / 2;
			break;
		case AV_PIX_FMT_YUV420P9:
		case AV_PIX_FMT_YUV420P10:
		case AV_PIX_FMT_YUV420P12:
		case AV_PIX_FMT_YUV420P14:
		case AV_PIX_FMT_YUV420P16:
			bytesLumaLine = dst->width * 2;
			bytesChromaLine = dst->width * 2 / 2;
			chromaHeight = dst->height / 2;
			break;
		case AV_PIX_FMT_YUV422P:
			bytesLumaLine = dst->width;
			bytesChromaLine = dst->width / 2;
			chromaHeight = dst->height;
			break;
		case AV_PIX_FMT_YUV422P9:
		case AV_PIX_FMT_YUV422P10:
		case AV_PIX_FMT_YUV422P12:
		case AV_PIX_FMT_YUV422P14:
		case AV_PIX_FMT_YUV422P16:
			bytesLumaLine = dst->width * 2;
			bytesChromaLine = dst->width * 2 / 2;
			chromaHeight = dst->height;
			break;
		default:
			THROWF(FormatException,
				"makeFrameFromFields: unsupported pixel format (%d)", dst->format);
		}

		uint8_t* dsty = dst->data[0];
		uint8_t* dstu = dst->data[1];
		uint8_t* dstv = dst->data[2];
		uint8_t* topy = top->data[0];
		uint8_t* topu = top->data[1];
		uint8_t* topv = top->data[2];
		uint8_t* bottomy = bottom->data[0];
		uint8_t* bottomu = bottom->data[1];
		uint8_t* bottomv = bottom->data[2];

		int stepdsty = dst->linesize[0];
		int stepdstu = dst->linesize[1];
		int stepdstv = dst->linesize[2];
		int steptopy = top->linesize[0];
		int steptopu = top->linesize[1];
		int steptopv = top->linesize[2];
		int stepbottomy = bottom->linesize[0];
		int stepbottomu = bottom->linesize[1];
		int stepbottomv = bottom->linesize[2];

		// luma
		for (int i = 0; i < dst->height; i += 2) {
			memcpy(dsty + stepdsty * (i + 0), topy + steptopy * (i + 0), bytesLumaLine);
			memcpy(dsty + stepdsty * (i + 1), bottomy + stepbottomy * (i + 1), bytesLumaLine);
		}

		// chroma
		for (int i = 0; i < chromaHeight; i += 2) {
			memcpy(dstu + stepdstu * (i + 0), topu + steptopu * (i + 0), bytesChromaLine);
			memcpy(dstu + stepdstu * (i + 1), bottomu + stepbottomu * (i + 1), bytesChromaLine);
			memcpy(dstv + stepdstv * (i + 0), topv + steptopv * (i + 0), bytesChromaLine);
			memcpy(dstv + stepdstv * (i + 1), bottomv + stepbottomv * (i + 1), bytesChromaLine);
		}

		return std::move(dstframe);
	}
};

class AMTMuxder : public AMTObject {
public:
	AMTMuxder(
		AMTContext&ctx,
		const TranscoderSetting& setting,
		const StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, audioCache_(ctx, setting.audioFilePath, reformInfo.getAudioFileOffsets(), 12, 4)
		, totalOutSize_(0)
	{ }

	void mux(int videoFileIndex) {
		int numEncoders = reformInfo_.getNumEncoders(videoFileIndex);
		if (numEncoders == 0) {
			return;
		}

		for (int i = 0; i < numEncoders; ++i) {
			// �����t�@�C�����쐬
			std::vector<std::string> audioFiles;
			const FileAudioFrameList& fileFrameList =
				reformInfo_.getFileAudioFrameList(i, videoFileIndex);
			for (int a = 0; a < (int)fileFrameList.size(); ++a) {
				const std::vector<int>& frameList = fileFrameList[a];
				if (frameList.size() > 0) {
					std::string filepath = setting_.getIntAudioFilePath(videoFileIndex, i, a);
					File file(filepath, "wb");
					for (int frameIndex : frameList) {
						file.write(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath);
				}
			}

			// Mux
			int outFileIndex = reformInfo_.getOutFileIndex(i, videoFileIndex);
			std::string encVideoFile = setting_.getEncVideoFilePath(videoFileIndex, i);
			std::string outFilePath = reformInfo_.isVFR()
				? setting_.getVfrTmpFilePath(outFileIndex)
				: setting_.getOutFilePath(outFileIndex);
			std::string args = makeMuxerArgs(
				setting_.muxerPath, encVideoFile, audioFiles, outFilePath);
			ctx.info("[Mux�J�n]");
			ctx.info(args.c_str());

			{
				MySubProcess muxer(args);
				int ret = muxer.join();
				if (ret != 0) {
					THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
				}
			}

			// VFR�̏ꍇ�̓^�C���R�[�h�𖄂ߍ���
			if (reformInfo_.isVFR()) {
				std::string outWithTimeFilePath = setting_.getOutFilePath(outFileIndex);
				std::string encTimecodeFile = setting_.getEnvTimecodeFilePath(videoFileIndex, i);
				{ // �^�C���R�[�h�t�@�C���𐶐�
					std::ostringstream ss;
					ss << "# timecode format v2" << std::endl;
					const auto& timecode = reformInfo_.getTimecode(i, videoFileIndex);
					for (int64_t pts : timecode) {
						double ms = ((double)pts / (MPEG_CLOCK_HZ / 1000));
						ss << (int)std::round(ms) << std::endl;
					}
					std::string str = ss.str();
					MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
					File file(encTimecodeFile, "w");
					file.write(mc);
				}
				std::string args = makeTimelineEditorArgs(
					setting_.timelineditorPath, outFilePath, outWithTimeFilePath, encTimecodeFile);
				ctx.info("[�^�C���R�[�h���ߍ��݊J�n]");
				ctx.info(args.c_str());
				{
					MySubProcess timelineeditor(args);
					int ret = timelineeditor.join();
					if (ret != 0) {
						THROWF(RuntimeException, "timelineeditor failed (exit code: %d)", ret);
					}
				}
				// ���ԃt�@�C���폜
				remove(encTimecodeFile.c_str());
				remove(outFilePath.c_str());
			}

			{ // �o�̓T�C�Y�擾
				File outfile(setting_.getOutFilePath(outFileIndex), "rb");
				totalOutSize_ += outfile.size();
			}

			// ���ԃt�@�C���폜
			for (const std::string& audioFile : audioFiles) {
				remove(audioFile.c_str());
			}
			remove(encVideoFile.c_str());
		}
	}

	int64_t getTotalOutSize() const {
		return totalOutSize_;
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const std::string& args) : EventBaseSubProcess(args) { }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// ����̓}���`�X���b�h�ŌĂ΂��̒���
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	};

	const TranscoderSetting& setting_;
	const StreamReformInfo& reformInfo_;

	PacketCache audioCache_;
	int64_t totalOutSize_;
};

static std::vector<char> toUTF8String(const std::string str) {
	if (str.size() == 0) {
		return std::vector<char>();
	}
	int intlen = (int)str.size() * 2;
	auto wc = std::unique_ptr<wchar_t[]>(new wchar_t[intlen]);
	intlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), wc.get(), intlen);
	if (intlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	int dstlen = WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, NULL, 0, NULL, NULL);
	if (dstlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, ret.data(), (int)ret.size(), NULL, NULL);
	return ret;
}

static std::string toJsonString(const std::string str) {
	if (str.size() == 0) {
		return str;
	}
	std::vector<char> utf8 = toUTF8String(str);
	std::vector<char> ret;
	for (char c : utf8) {
		switch (c) {
		case '\"':
			ret.push_back('\\');
			ret.push_back('\"');
			break;
		case '\\':
			ret.push_back('\\');
			ret.push_back('\\');
			break;
		case '/':
			ret.push_back('\\');
			ret.push_back('/');
			break;
		case '\b':
			ret.push_back('\\');
			ret.push_back('b');
			break;
		case '\f':
			ret.push_back('\\');
			ret.push_back('f');
			break;
		case '\n':
			ret.push_back('\\');
			ret.push_back('n');
			break;
		case '\r':
			ret.push_back('\\');
			ret.push_back('r');
			break;
		case '\t':
			ret.push_back('\\');
			ret.push_back('t');
			break;
		default:
			ret.push_back(c);
		}
	}
	return std::string(ret.begin(), ret.end());
}

static void transcodeMain(AMTContext& ctx, const TranscoderSetting& setting)
{
	setting.dump(ctx);

	auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
	if (setting.serviceId > 0) {
		splitter->setServiceId(setting.serviceId);
	}
	StreamReformInfo reformInfo = splitter->split();
	int64_t totalIntVideoSize = splitter->getTotalIntVideoSize();
	int64_t srcFileSize = splitter->getSrcFileSize();
	splitter = nullptr;

	if (setting.dumpStreamInfo) {
		reformInfo.serialize(setting.getStreamInfoPath());
	}

	reformInfo.prepareEncode();

	auto encoder = std::unique_ptr<AMTVideoEncoder>(new AMTVideoEncoder(ctx, setting, reformInfo));
	for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
		encoder->encode(i);
	}
	encoder = nullptr;

	auto audioDiffInfo = reformInfo.prepareMux();
	audioDiffInfo.printAudioPtsDiff(ctx);

	auto muxer = std::unique_ptr<AMTMuxder>(new AMTMuxder(ctx, setting, reformInfo));
	for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
		muxer->mux(i);
	}
	int64_t totalOutSize = muxer->getTotalOutSize();
	muxer = nullptr;

	// ���ԃt�@�C�����폜
	remove(setting.audioFilePath.c_str());

	// �o�͌��ʂ�\��
	ctx.info("����");
	reformInfo.printOutputMapping([&](int index) { return setting.getOutFilePath(index); });

	// �o�͌���JSON�o��
	if (setting.outInfoJsonPath.size() > 0) {
		std::ostringstream ss;
		ss << "{ \"srcpath\": \"" << toJsonString(setting.tsFilePath) << "\", ";
		ss << "\"outpath\": [";
		for (int i = 0; i < reformInfo.getNumOutFiles(); ++i) {
			if (i > 0) {
				ss << ", ";
			}
			ss << "\"" << toJsonString(setting.getOutFilePath(i)) << "\"";
		}
		ss << "], ";
		ss << "\"srcfilesize\": " << srcFileSize << ", ";
		ss << "\"intvideofilesize\": " << totalIntVideoSize << ", ";
		ss << "\"outfilesize\": " << totalOutSize << ", ";
		auto duration = reformInfo.getInOutDuration();
		ss << "\"srcduration\": " << std::fixed << std::setprecision(3)
			 << ((double)duration.first / MPEG_CLOCK_HZ) << ", ";
		ss << "\"outduration\": " << std::fixed << std::setprecision(3)
			 << ((double)duration.second / MPEG_CLOCK_HZ) << ", ";
		ss << "\"audiodiff\": ";
		audioDiffInfo.printToJson(ss);
		ss << " }";

		std::string str = ss.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.outInfoJsonPath, "w");
		file.write(mc);
	}
}
