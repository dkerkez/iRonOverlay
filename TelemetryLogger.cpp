#include "TelemetryLogger.h"

#include <cstdio>
#include <ctime>
#include "Config.h"

static std::string formatFloat(float value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    return std::string(buf);
}

static std::string formatInt(int value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return std::string(buf);
}

struct LoggedField
{
    const char* name;
    std::string (*readValue)();
};

static const std::vector<LoggedField> kLoggedFields = {
    {"ir_Lap", []() -> std::string { return formatInt(ir_Lap.getInt()); }},
    {"ir_LapCurrentLapTime", []() -> std::string { return formatFloat(ir_LapCurrentLapTime.getFloat()); }},
    {"ir_LapDistPct", []() -> std::string { return formatFloat(ir_LapDistPct.getFloat()); }},
    {"ir_Throttle", []() -> std::string { return formatFloat(ir_Throttle.getFloat()); }},
    {"ir_Brake", []() -> std::string { return formatFloat(ir_Brake.getFloat()); }},
    {"ir_BrakeABSactive", []() -> std::string { return formatInt(ir_BrakeABSactive.getBool() ? 1 : 0); }},
    {"ir_SteeringWheelAngle", []() -> std::string { return formatFloat(ir_SteeringWheelAngle.getFloat()); }},
    {"ir_Gear", []() -> std::string { return formatInt(ir_Gear.getInt()); }},
    {"ir_Speed", []() -> std::string { return formatFloat(ir_Speed.getFloat()); }},
    {"ir_Yaw", []() -> std::string { return formatFloat(ir_Yaw.getFloat()); }},
    {"ir_Pitch", []() -> std::string { return formatFloat(ir_Pitch.getFloat()); }},
    {"ir_Roll", []() -> std::string { return formatFloat(ir_Roll.getFloat()); }},
    {"ir_YawRate", []() -> std::string { return formatFloat(ir_YawRate.getFloat()); }}
};

void TelemetryLogger::update(ConnectionStatus status)
{
    if (!g_cfg.getBool("TelemetryLogger", "enabled", true))
    {
        flushLapBuffer();
        m_currentLap = -1;
        m_haveLastValues = false;
        m_currentSectorIdx = -1;
        m_sectorRows.clear();
        return;
    }

    if (status != ConnectionStatus::DRIVING)
    {
        flushLapBuffer();
        m_currentLap = -1;
        m_haveLastValues = false;
        m_currentSectorIdx = -1;
        m_sectorRows.clear();
        return;
    }

    const std::string newFileStem = buildFileStem();
    if (m_fileStem != newFileStem)
    {
        flushLapBuffer();
        m_fileStem = newFileStem;
        m_currentLap = -1;
        m_haveLastValues = false;
        m_currentSectorIdx = -1;
        m_sectorRows.clear();
        m_bestSectorTimes.clear();
        loadBestSectorTimes();
    }

    const int lap = ir_Lap.getInt();
    if (m_currentLap < 0)
        m_currentLap = lap;

    if (lap != m_currentLap)
    {
        flushLapBuffer();
        m_currentLap = lap;
        m_haveLastValues = false;
        m_currentSectorIdx = -1;
        m_sectorRows.clear();
    }

    updateSectorTracking();

    const std::vector<std::string> values = readCurrentValues();
    if (!m_haveLastValues || values != m_lastValues)
    {
        appendCurrentRow(values);

        if (m_currentSectorIdx >= 0)
        {
            m_sectorRows[m_currentSectorIdx].push_back(buildRow(values));
        }

        m_lastValues = values;
        m_haveLastValues = true;
    }
}

std::vector<std::string> TelemetryLogger::readCurrentValues() const
{
    std::vector<std::string> values;
    values.reserve(kLoggedFields.size());
    for (const LoggedField& field : kLoggedFields)
        values.push_back(field.readValue());
    return values;
}

void TelemetryLogger::appendCurrentRow(const std::vector<std::string>& values)
{
    m_lapRows.push_back(buildRow(values));
}

void TelemetryLogger::flushLapBuffer()
{
    if (m_lapRows.empty())
        return;

    const std::string stem = m_fileStem.empty() ? buildFileStem() : m_fileStem;
    const std::string fullPath = buildFullPath(stem);
    const std::string header = buildHeader();

    appendLinesToFile(fullPath, header, m_lapRows);
    m_lapRows.clear();
}

std::string TelemetryLogger::buildHeader()
{
    std::string line;
    for (size_t i = 0; i < kLoggedFields.size(); ++i)
    {
        if (i > 0)
            line += ",";
        line += kLoggedFields[i].name;
    }
    return line;
}

std::string TelemetryLogger::buildRow(const std::vector<std::string>& values)
{
    std::string line;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            line += ",";
        line += values[i];
    }
    return line;
}

std::string TelemetryLogger::buildFileStem()
{
    const std::string trackName = sanitizeFilePart(ir_session.trackName.empty() ? "unknownTrack" : ir_session.trackName);
    const std::string carName = sanitizeFilePart(ir_session.carName.empty() ? "unknownCar" : ir_session.carName);
    return buildDatePrefix() + "_" + trackName + "_" + carName;
}

std::string TelemetryLogger::buildFullPath(const std::string& stem)
{
    std::string savePath = g_cfg.getString("TelemetryLogger", "save_path", "");

    if (!savePath.empty())
    {
        if (savePath.back() != '\\' && savePath.back() != '/')
            savePath += "\\";
        return savePath + stem + ".csv";
    }

    return stem + ".csv";
}

std::string TelemetryLogger::buildDatePrefix()
{
    std::time_t now = std::time(nullptr);
    std::tm localTm = {};
    localtime_s(&localTm, &now);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
    return std::string(buf);
}

std::string TelemetryLogger::sanitizeFilePart(const std::string& value)
{
    std::string out = value;

    for (char& c : out)
    {
        const bool invalid = (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*');
        if (invalid || c == ' ')
            c = '_';
    }

    if (out.empty())
        return "unknown";

    while (!out.empty() && out.back() == '_')
        out.pop_back();

    return out.empty() ? std::string("unknown") : out;
}

bool TelemetryLogger::fileHasContent(const std::string& filePath)
{
    FILE* fp = std::fopen(filePath.c_str(), "rb");
    if (!fp)
        return false;

    const int seekOk = std::fseek(fp, 0, SEEK_END);
    const long size = (seekOk == 0) ? std::ftell(fp) : 0;
    std::fclose(fp);
    return size > 0;
}

void TelemetryLogger::appendLinesToFile(const std::string& filePath, const std::string& header, const std::vector<std::string>& lines)
{
    if (lines.empty())
        return;

    const bool hasContent = fileHasContent(filePath);

    FILE* fp = std::fopen(filePath.c_str(), "ab");
    if (!fp)
        return;

    if (!hasContent)
        std::fprintf(fp, "%s\n", header.c_str());

    for (const std::string& line : lines)
        std::fprintf(fp, "%s\n", line.c_str());

    std::fclose(fp);
}

int TelemetryLogger::getCurrentSectorIndex() const
{
    const float lapDistPct = ir_LapDistPct.getFloat();
    if (lapDistPct < 0.0f)
        return -1;

    if (ir_session.sectors.empty())
        return -1;

    for (int i = (int)ir_session.sectors.size() - 1; i >= 0; --i)
    {
        if (lapDistPct >= ir_session.sectors[i].sectorStartPct)
            return i;
    }

    return -1;
}

void TelemetryLogger::updateSectorTracking()
{
    const float lapDistPct = ir_LapDistPct.getFloat();

    if (lapDistPct == 0.0f)
    {
        m_currentSectorIdx = -1;
        m_sectorStartTime = 0.0f;
        return;
    }

    const int newSectorIdx = getCurrentSectorIndex();

    if (newSectorIdx != m_currentSectorIdx && newSectorIdx >= 0)
    {
        if (m_currentSectorIdx >= 0)
        {
            const float currentTime = ir_LapCurrentLapTime.getFloat();
            const float sectorTime = currentTime - m_sectorStartTime;

            if (sectorTime > 0.0f)
            {
                bool isBestSector = false;

                if (m_bestSectorTimes.find(m_currentSectorIdx) == m_bestSectorTimes.end())
                {
                    isBestSector = true;
                }
                else if (sectorTime < m_bestSectorTimes[m_currentSectorIdx])
                {
                    isBestSector = true;
                }

                if (isBestSector)
                {
                    m_bestSectorTimes[m_currentSectorIdx] = sectorTime;
                    saveBestSectorTimes();
                    flushSectorBuffer(m_currentSectorIdx);
                }
                else
                {
                    m_sectorRows[m_currentSectorIdx].clear();
                }
            }
        }

        m_currentSectorIdx = newSectorIdx;
        m_sectorStartTime = ir_LapCurrentLapTime.getFloat();
        m_sectorRows[newSectorIdx].clear();
    }
}

void TelemetryLogger::flushSectorBuffer(int sectorIdx)
{
    if (m_sectorRows.find(sectorIdx) == m_sectorRows.end() || m_sectorRows[sectorIdx].empty())
        return;

    const std::string stem = m_fileStem.empty() ? buildFileStem() : m_fileStem;
    const std::string fullPath = buildSectorFilePath(stem, sectorIdx);
    const std::string header = buildHeader();

    const bool hasContent = fileHasContent(fullPath);

    FILE* fp = std::fopen(fullPath.c_str(), "wb");
    if (!fp)
        return;

    std::fprintf(fp, "%s\n", header.c_str());

    for (const std::string& line : m_sectorRows[sectorIdx])
        std::fprintf(fp, "%s\n", line.c_str());

    std::fclose(fp);

    m_sectorRows[sectorIdx].clear();
}

std::string TelemetryLogger::buildSectorFilePath(const std::string& stem, int sectorIdx)
{
    std::string savePath = g_cfg.getString("TelemetryLogger", "save_path", "");

    const std::string trackName = sanitizeFilePart(ir_session.trackName.empty() ? "unknownTrack" : ir_session.trackName);
    const std::string carName = sanitizeFilePart(ir_session.carName.empty() ? "unknownCar" : ir_session.carName);

    char sectorSuffix[32];
    std::snprintf(sectorSuffix, sizeof(sectorSuffix), "_optimal_sector%d", sectorIdx + 1);

    if (!savePath.empty())
    {
        if (savePath.back() != '\\' && savePath.back() != '/')
            savePath += "\\";
        return savePath + trackName + "_" + carName + sectorSuffix + ".csv";
    }

    return trackName + "_" + carName + sectorSuffix + ".csv";
}

std::string TelemetryLogger::buildSectorTimesFilePath(const std::string& stem)
{
    std::string savePath = g_cfg.getString("TelemetryLogger", "save_path", "");

    const std::string trackName = sanitizeFilePart(ir_session.trackName.empty() ? "unknownTrack" : ir_session.trackName);
    const std::string carName = sanitizeFilePart(ir_session.carName.empty() ? "unknownCar" : ir_session.carName);

    if (!savePath.empty())
    {
        if (savePath.back() != '\\' && savePath.back() != '/')
            savePath += "\\";
        return savePath + trackName + "_" + carName + "_optimal_times.txt";
    }

    return trackName + "_" + carName + "_optimal_times.txt";
}

void TelemetryLogger::loadBestSectorTimes()
{
    const std::string filePath = buildSectorTimesFilePath(m_fileStem);

    FILE* fp = std::fopen(filePath.c_str(), "rb");
    if (!fp)
        return;

    m_bestSectorTimes.clear();

    char line[256];
    while (std::fgets(line, sizeof(line), fp))
    {
        int sectorIdx = -1;
        float sectorTime = 0.0f;

        if (std::sscanf(line, "%d:%f", &sectorIdx, &sectorTime) == 2)
        {
            if (sectorIdx >= 0 && sectorTime > 0.0f)
            {
                m_bestSectorTimes[sectorIdx] = sectorTime;
            }
        }
    }

    std::fclose(fp);
}

void TelemetryLogger::saveBestSectorTimes()
{
    const std::string filePath = buildSectorTimesFilePath(m_fileStem);

    FILE* fp = std::fopen(filePath.c_str(), "wb");
    if (!fp)
        return;

    for (const auto& pair : m_bestSectorTimes)
    {
        std::fprintf(fp, "%d:%.6f\n", pair.first, pair.second);
    }

    std::fclose(fp);
}
