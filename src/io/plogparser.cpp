#include "io/plogparser.h"
#include <fstream>
#include <cstdint>
#include <cstring>
#include <sstream>

#include "3rdparty/zstd/zstd.h"

namespace io {

  namespace {
    constexpr uint32_t kPlogMagic = 0x474F4C50; // "PLOG" in Little Endian
    constexpr uint32_t kPlogVersion = 2;

    constexpr int32_t kPlogRealNaN = std::numeric_limits<int32_t>::min();
    constexpr int8_t kPlogBooleanNaN = 0xFF;
    constexpr double kScaleFactor = 1000.0;
  }

  bool PlogParser::parse(const std::string &path, core::LogSession &outSession, std::string &errorOut, std::atomic<float> *progress)
  {
    // Assumes same indianness between write and parse.

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      errorOut = "Could not open file: " + path;
      return false;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string compressed = buf.str();

    unsigned long long decompressedSize = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
      errorOut = "Not a valid zstd frame: " + path;
      return false;
    }
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
      errorOut = "Compressed frame has unknown content size: " + path;
      return false;
    }

    std::string raw(decompressedSize, '\0');
    size_t actualSize = ZSTD_decompress(raw.data(), decompressedSize, compressed.data(), compressed.size());
    if (ZSTD_isError(actualSize)) {
      errorOut = std::string("zstd decompression failed: ") + ZSTD_getErrorName(actualSize);
      return false;
    }

    std::istringstream inb(raw, std::ios::binary);

    uint32_t magic = 0;
    uint32_t version = 0;
    inb.read(reinterpret_cast<char *>(&magic), sizeof(magic));
    inb.read(reinterpret_cast<char *>(&version), sizeof(version));

    if (magic != kPlogMagic || version < 1 || version > kPlogVersion) {
      errorOut = "Unsupported or corrupt .plog format version (" + std::to_string(version) + "): " + path;
      return false;
    }

    uint64_t rowCount = 0;
    uint32_t channelCount = 0;
    int32_t timeChannelIdx = -1;

    inb.read(reinterpret_cast<char *>(&rowCount), sizeof(rowCount));
    inb.read(reinterpret_cast<char *>(&channelCount), sizeof(channelCount));
    inb.read(reinterpret_cast<char *>(&timeChannelIdx), sizeof(timeChannelIdx));

    auto readString = [&inb]() -> std::string {
      uint16_t len = 0;
      inb.read(reinterpret_cast<char *>(&len), sizeof(len));
      if (len == 0) return {};
      std::string s(len, '\0');
      inb.read(s.data(), len);
      return s;
      };

    std::string sourcePath = readString();
    std::string formatInfo = readString();
    std::string captureDate = readString();

    struct ChannelMeta {
      std::string name;
      std::string unit;
      uint8_t flags = 0; // bit 0: isNumeric, bit 1: isBoolean, bit 2: isCustom
      std::string formula;
    };

    std::vector<ChannelMeta> metas(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i) {
      metas[i].name = readString();
      metas[i].unit = readString();
      inb.read(reinterpret_cast<char *>(&metas[i].flags), sizeof(metas[i].flags));
      metas[i].formula = readString();
    }

    uint64_t nofStitches = 0;
    inb.read(reinterpret_cast<char *>(&nofStitches), sizeof(nofStitches));
    std::vector<double> stitchPoints(nofStitches);
    if (0 < nofStitches) {
      inb.read(reinterpret_cast<char *>(stitchPoints.data()), stitchPoints.size() * sizeof(double));
    }

    std::vector<core::Annotation> annotations;
    if (2 <= kPlogVersion) {
      uint64_t nofAnnots;
      inb.read(reinterpret_cast<char *>(&nofAnnots), sizeof(nofAnnots));
      annotations.resize(nofAnnots);
      for (auto &a : annotations) {
        inb.read(reinterpret_cast<char *>(&a.timeSec), sizeof(double));
        a.label = readString();
        inb.read(reinterpret_cast<char *>(&a.color), sizeof(ImVec4));
      }
    }

    std::vector<core::Channel> channels;
    channels.reserve(channelCount);

    for (uint32_t i = 0; i < channelCount; ++i) {
      core::Channel ch(metas[i].name, metas[i].unit);
      ch.setIsNumeric((metas[i].flags & 1) != 0);
      ch.setIsBoolean((metas[i].flags & 2) != 0);
      ch.setIsCustom((metas[i].flags & 4) != 0);
      ch.setFormula(metas[i].formula);

      ch.values().resize(rowCount);
      if (ch.isBoolean()) {
        std::vector<uint8_t> scaledData(rowCount);
        inb.read(reinterpret_cast<char *>(scaledData.data()), rowCount * sizeof(uint8_t));
        for (size_t r = 0; r < rowCount; ++r) {
          ch.values()[r] = (scaledData[r] == kPlogBooleanNaN)
            ? std::numeric_limits<double>::quiet_NaN()
            : (scaledData[r] != 0 ? 1.0 : 0.0);
        }
      } else {
        std::vector<int32_t> scaledData(rowCount);
        inb.read(reinterpret_cast<char *>(scaledData.data()), rowCount * sizeof(int32_t));
        for (size_t r = 0; r < rowCount; ++r) {
          ch.values()[r] = (scaledData[r] == kPlogRealNaN)
            ? std::numeric_limits<double>::quiet_NaN()
            : static_cast<double>(scaledData[r]) / kScaleFactor;
        }
      }

      channels.push_back(std::move(ch));

      if (progress) {
        progress->store(static_cast<float>(i + 1) / static_cast<float>(channelCount));
      }
    }

    outSession = core::LogSession();
    outSession.setSourcePath(path);
    outSession.setFormatInfo(formatInfo);
    outSession.setCaptureDate(captureDate);
    outSession.setRowCount(rowCount);
    if (timeChannelIdx >= 0 && static_cast<size_t>(timeChannelIdx) < channels.size()) {
      outSession.setTimeChannelIndex(static_cast<size_t>(timeChannelIdx));
    }
    outSession.setStitchPoints(stitchPoints);
    outSession.setAnnotations(annotations);

    for (auto &ch : channels) {
      outSession.addChannel(std::move(ch));
    }

    if (progress) progress->store(1.0f);
    return true;
  }

  bool PlogParser::write(const std::string &path, const core::LogSession &session, std::string &errorOut, std::atomic<float> *progress)
  {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
      errorOut = "Could not open file for writing: " + path;
      return false;
    }

    std::ostringstream ss;
    std::ostream &out = ss;

    out.write(reinterpret_cast<const char *>(&kPlogMagic), sizeof(kPlogMagic));
    out.write(reinterpret_cast<const char *>(&kPlogVersion), sizeof(kPlogVersion));

    uint64_t rowCount = session.rowCount();
    uint32_t channelCount = static_cast<uint32_t>(session.channels().size());
    int32_t timeChannelIdx = -1;

    if (auto ti = session.timeChannelIndex()) {
      timeChannelIdx = ti.value();
    }

    out.write(reinterpret_cast<const char *>(&rowCount), sizeof(rowCount));
    out.write(reinterpret_cast<const char *>(&channelCount), sizeof(channelCount));
    out.write(reinterpret_cast<const char *>(&timeChannelIdx), sizeof(timeChannelIdx));

    auto writeString = [&out](const std::string &s) {
      uint16_t len = static_cast<uint16_t>(s.size());
      out.write(reinterpret_cast<const char *>(&len), sizeof(len));
      if (len > 0) {
        out.write(s.data(), len);
      }
      };

    writeString(session.sourcePath());
    writeString(session.formatInfo());
    writeString(session.captureDate());

    for (const auto &ch : session.channels()) {
      writeString(ch.name());
      writeString(ch.unit());

      uint8_t flags = 0;
      if (ch.isNumeric()) flags |= 1;
      if (ch.isBoolean()) flags |= 2;
      if (ch.isCustom())  flags |= 4;

      out.write(reinterpret_cast<const char *>(&flags), sizeof(flags));
      writeString(ch.formula());
    }

    const auto &stitchPoints = session.stitchPoints();
    uint64_t nofStitches = stitchPoints.size();
    out.write(reinterpret_cast<const char *>(&nofStitches), sizeof(nofStitches));
    out.write(reinterpret_cast<const char *>(stitchPoints.data()), stitchPoints.size() * sizeof(double));

    uint64_t nofAnnots = session.annotations().size();
    out.write(reinterpret_cast<const char *>(&nofAnnots), sizeof(nofAnnots));
    for (const auto &a : session.annotations()) {
      out.write(reinterpret_cast<const char *>(&a.timeSec), sizeof(double));
      writeString(a.label);
      out.write(reinterpret_cast<const char *>(&a.color), sizeof(ImVec4));
    }

    for (size_t i = 0; i < session.channels().size(); ++i) {
      const auto &ch = session.channels()[i];
      if (ch.isBoolean()) {
        std::vector<uint8_t> boolData(rowCount, kPlogBooleanNaN);
        for (size_t r = 0; r < rowCount && r < ch.values().size(); ++r) {
          double v = ch.values()[r];
          if (!std::isnan(v)) {
            boolData[r] = (v != 0.0) ? 1 : 0;
          }
        }
        out.write(reinterpret_cast<const char *>(boolData.data()), rowCount * sizeof(uint8_t));
      } else {
        std::vector<int32_t> scaledData(rowCount, kPlogRealNaN);
        for (size_t r = 0; r < rowCount && r < ch.values().size(); ++r) {
          double v = ch.values()[r];
          if (!std::isnan(v)) {
            scaledData[r] = static_cast<int32_t>(std::round(v * kScaleFactor));
          }
        }
        out.write(reinterpret_cast<const char *>(scaledData.data()), rowCount * sizeof(int32_t));
      }

      if (progress) {
        progress->store(static_cast<float>(i + 1) / static_cast<float>(channelCount));
      }
    }

    std::string str = ss.str();
    auto rawSize = str.size();
    auto rawData = str.data();
    size_t bound = ZSTD_compressBound(rawSize);
    std::vector<char> compressed(bound);
    size_t compressedSize = ZSTD_compress(
      compressed.data(), bound,
      rawData, rawSize,
      5
    );

    file.write(compressed.data(), compressedSize);

    if (progress) progress->store(1.0f);
    return true;
  }

} // namespace io