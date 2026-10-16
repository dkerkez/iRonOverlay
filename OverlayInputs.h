/*
MIT License

Copyright (c) 2021-2022 L. E. Spalt

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Overlay.h"
#include "Config.h"
#include "OverlayDebug.h"
#include "iracing.h"

class OverlayInputs : public Overlay
{
public:
	OverlayInputs()
		: Overlay("OverlayInputs")
	{
	}

protected:
	virtual float2 getDefaultSize()
	{
		return float2(400, 100);
	}

	virtual void onConfigChanged()
	{
		// Width might have changed, reset tracker values
		m_throttleVtx.resize(m_width);
		m_brakeVtx.resize(m_width);
		m_steerVtx.resize(m_width);
		m_absActiveVtx.resize(m_width);
		// Ensure delta vertex buffer is resized before we write into it
		m_deltaVtx.resize(m_width);

		for (int i = 0; i < m_width; ++i)
		{
			m_throttleVtx[i].x = float(i);
			m_brakeVtx[i].x = float(i);
			m_steerVtx[i].x = float(i);
			m_deltaVtx[i].x = float(i);
		}

		// Read config flag from global config
		m_showDelta = g_cfg.getBool(getName(), "show_delta", true);
	}

	virtual void onUpdate()
	{
		const float w = (float)m_width;
		const float h = (float)m_height;

		// Make code below safe against indexing into size-1 when sizes are zero
		if (m_throttleVtx.empty())
			m_throttleVtx.resize(1);
		if (m_brakeVtx.empty())
			m_brakeVtx.resize(1);
		if (m_steerVtx.empty())
			m_steerVtx.resize(1);
		if (m_absActiveVtx.empty())
			m_absActiveVtx.resize(1);
		if (m_deltaVtx.empty())
			m_deltaVtx.resize(1);

		// Advance input vertices
		{
			for (int i = 0; i < (int)m_throttleVtx.size() - 1; ++i)
				m_throttleVtx[i].y = m_throttleVtx[i + 1].y;
			m_throttleVtx[(int)m_throttleVtx.size() - 1].y = ir_Throttle.getFloat();

			for (int i = 0; i < (int)m_brakeVtx.size() - 1; ++i)
			{
				m_brakeVtx[i].y = m_brakeVtx[i + 1].y;
				m_absActiveVtx[i] = m_absActiveVtx[i + 1];
			}
			m_brakeVtx[(int)m_brakeVtx.size() - 1].y = ir_Brake.getFloat();
			m_absActiveVtx[(int)m_absActiveVtx.size() - 1] = ir_BrakeABSactive.getBool();
			// ABS ir_BrakeABSactive.getFloat();

			for (int i = 0; i < (int)m_steerVtx.size() - 1; ++i)
				m_steerVtx[i].y = m_steerVtx[i + 1].y;
			m_steerVtx[(int)m_steerVtx.size() - 1].y = std::min(0.98f, std::max(0.02f, (ir_SteeringWheelAngle.getFloat() / 3.6f) * -0.5f + 0.5f));

          // Delta (sector delta) - store change in seconds (clamped to [-1,1])
			for (int i = 0; i < (int)m_deltaVtx.size() - 1; ++i)
				m_deltaVtx[i].y = m_deltaVtx[i + 1].y;
			const bool isValid = ir_LapDeltaToOptimalLap_OK.getBool();
			const float deltaVal = ir_LapDeltaToOptimalLap.getFloat();
			// determine current sector based on lap distance
			const float lapDistPct = ir_LapDistPct.getFloat();
			int newSectorIdx = -1;
			for (int i = (int)ir_session.sectors.size() - 1; i >= 0; --i)
			{
				if (lapDistPct >= ir_session.sectors[i].sectorStartPct)
				{
					newSectorIdx = i;
					break;
				}
			}
			if (newSectorIdx != m_currentSectorIdx && newSectorIdx > -1)
			{
				m_currentSectorIdx = newSectorIdx;
				m_sectorStartDelta = isValid ? deltaVal : 0.0f;
				m_sectorStartValid = isValid;
			}
			else if (isValid && !m_sectorStartValid && m_currentSectorIdx >= 0)
			{
				m_sectorStartDelta = deltaVal;
				m_sectorStartValid = true;
			}
			float sectorDelta = 0.0f;
			if (m_currentSectorIdx >= 0 && isValid && m_sectorStartValid)
			{
				sectorDelta = deltaVal - m_sectorStartDelta;
			}
			// Trim to +/- 1 second
			if (sectorDelta > 0.5f) sectorDelta = 0.5f;
			if (sectorDelta < -0.5f) sectorDelta = -0.5f;
			m_deltaVtx[(int)m_deltaVtx.size() - 1].y = sectorDelta;
		}

		const float thickness = g_cfg.getFloat(m_name, "line_thickness", 2.0f);
     // Split vertical area: top part for delta (1/3), bottom for other graphs (2/3)
		const float deltaH = h / 3.0f;
		const float bottomH = h - deltaH;
		auto vtx2coordBottom = [&](const float2 &v) -> float2
		{
			// v.y in [0..1]
			return float2(v.x + 0.5f, h - 0.5f * thickness - v.y * (bottomH - thickness));
		};
		auto vtx2coordDelta = [&](const float2 &v) -> float2
		{
			// v.y in [-1..1], map 0 to center of delta area
			float centerY = deltaH * 0.5f;
			float offset = -(v.y / 0.5f) * (deltaH * 0.5f);
			float y = centerY + offset;
			// add small padding similar to other graphs
			return float2(v.x + 0.5f, y + 0.5f * thickness);
		};

		// Throttle (fill)
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> throttleFillPath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> throttleFillSink;
		m_d2dFactory->CreatePathGeometry(&throttleFillPath);
		throttleFillPath->Open(&throttleFillSink);
		throttleFillSink->BeginFigure(float2(0, h), D2D1_FIGURE_BEGIN_FILLED);
     for (int i = 0; i < (int)m_throttleVtx.size(); ++i)
			throttleFillSink->AddLine(vtx2coordBottom(m_throttleVtx[i]));
		throttleFillSink->AddLine(float2(m_throttleVtx[m_throttleVtx.size() - 1].x + 0.5f, h));
		throttleFillSink->EndFigure(D2D1_FIGURE_END_OPEN);
		throttleFillSink->Close();

		// Brake (fill)
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> brakeFillPath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> brakeFillSink;
		m_d2dFactory->CreatePathGeometry(&brakeFillPath);
		brakeFillPath->Open(&brakeFillSink);
		brakeFillSink->BeginFigure(float2(0, h), D2D1_FIGURE_BEGIN_FILLED);
        for (int i = 0; i < (int)m_brakeVtx.size(); ++i)
			brakeFillSink->AddLine(vtx2coordBottom(m_brakeVtx[i]));
		brakeFillSink->AddLine(float2(m_brakeVtx[m_brakeVtx.size() - 1].x + 0.5f, h));
		brakeFillSink->EndFigure(D2D1_FIGURE_END_OPEN);
		brakeFillSink->Close();

		// Throttle (line)
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> throttleLinePath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> throttleLineSink;
		m_d2dFactory->CreatePathGeometry(&throttleLinePath);
		throttleLinePath->Open(&throttleLineSink);
       throttleLineSink->BeginFigure(vtx2coordBottom(m_throttleVtx[0]), D2D1_FIGURE_BEGIN_HOLLOW);
		for (int i = 1; i < (int)m_throttleVtx.size(); ++i)
			throttleLineSink->AddLine(vtx2coordBottom(m_throttleVtx[i]));
		throttleLineSink->EndFigure(D2D1_FIGURE_END_OPEN);
		throttleLineSink->Close();

		// Brake (line)
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> brakeLinePath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> brakeLineSink;
		m_d2dFactory->CreatePathGeometry(&brakeLinePath);
		brakeLinePath->Open(&brakeLineSink);
     brakeLineSink->BeginFigure(vtx2coordBottom(m_brakeVtx[0]), D2D1_FIGURE_BEGIN_HOLLOW);
		for (int i = 1; i < (int)m_brakeVtx.size(); ++i)
			brakeLineSink->AddLine(vtx2coordBottom(m_brakeVtx[i]));
		brakeLineSink->EndFigure(D2D1_FIGURE_END_OPEN);
		brakeLineSink->Close();

		// Steering
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> steeringLinePath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> steeringLineSink;
		m_d2dFactory->CreatePathGeometry(&steeringLinePath);
		steeringLinePath->Open(&steeringLineSink);
      steeringLineSink->BeginFigure(vtx2coordBottom(m_steerVtx[0]), D2D1_FIGURE_BEGIN_HOLLOW);
		for (int i = 1; i < (int)m_steerVtx.size(); ++i)
			steeringLineSink->AddLine(vtx2coordBottom(m_steerVtx[i]));
		steeringLineSink->EndFigure(D2D1_FIGURE_END_OPEN);
		steeringLineSink->Close();

		// ABS Overlay (orange segments where ABS is active)
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> absOverlayPath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> absOverlaySink;
		m_d2dFactory->CreatePathGeometry(&absOverlayPath);
		absOverlayPath->Open(&absOverlaySink);
		for (int i = 0; i < (int)m_brakeVtx.size(); ++i)
		{
			if (m_absActiveVtx[i])
			{
				// Find the start of ABS active segment
				int startIdx = i;
				// Find the end of ABS active segment
				while (i < (int)m_brakeVtx.size() && m_absActiveVtx[i])
					++i;
				int endIdx = i - 1;

                // Draw line segment for this ABS active region
				absOverlaySink->BeginFigure(vtx2coordBottom(m_brakeVtx[startIdx]), D2D1_FIGURE_BEGIN_HOLLOW);
				for (int j = startIdx + 1; j <= endIdx; ++j)
                  absOverlaySink->AddLine(vtx2coordBottom(m_brakeVtx[j]));
				absOverlaySink->EndFigure(D2D1_FIGURE_END_OPEN);
			}
		}
		absOverlaySink->Close();

	   const bool showDelta = m_showDelta;
			   auto rising = [&](int i) -> bool
			   {
				   const int n = (int)m_deltaVtx.size();
				   if (i + 1 < n)
					   return m_deltaVtx[i + 1].y >= m_deltaVtx[i].y;
				   if (i > 0)
					   return m_deltaVtx[i].y >= m_deltaVtx[i - 1].y;
				   return true;
			   };
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> deltaPosPath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> deltaPosSink;
		Microsoft::WRL::ComPtr<ID2D1PathGeometry1> deltaNegPath;
		Microsoft::WRL::ComPtr<ID2D1GeometrySink> deltaNegSink;
		if (showDelta)
		{
			m_d2dFactory->CreatePathGeometry(&deltaPosPath);
			m_d2dFactory->CreatePathGeometry(&deltaNegPath);
			deltaPosPath->Open(&deltaPosSink);
			deltaNegPath->Open(&deltaNegSink);
			for (int i = 0; i < (int)m_deltaVtx.size(); ++i)
			{
				if (rising(i))
				{
					int start = i;
					while (i < (int)m_deltaVtx.size() && rising(i))
						++i;
					int end = std::min(i, (int)m_deltaVtx.size() - 1);
					deltaPosSink->BeginFigure(vtx2coordDelta(m_deltaVtx[start]), D2D1_FIGURE_BEGIN_HOLLOW);
					for (int j = start + 1; j <= end; ++j)
						deltaPosSink->AddLine(vtx2coordDelta(m_deltaVtx[j]));
					deltaPosSink->EndFigure(D2D1_FIGURE_END_OPEN);
					--i; // compensate outer loop increment
				}
				else
				{
					int start = i;
					while (i < (int)m_deltaVtx.size() && !rising(i))
						++i;
					int end = std::min(i, (int)m_deltaVtx.size() - 1);
					deltaNegSink->BeginFigure(vtx2coordDelta(m_deltaVtx[start]), D2D1_FIGURE_BEGIN_HOLLOW);
					for (int j = start + 1; j <= end; ++j)
						deltaNegSink->AddLine(vtx2coordDelta(m_deltaVtx[j]));
					deltaNegSink->EndFigure(D2D1_FIGURE_END_OPEN);
					--i;
				}
			}
			deltaPosSink->Close();
			deltaNegSink->Close();
		}

        m_renderTarget->BeginDraw();
		m_brush->SetColor(g_cfg.getFloat4(m_name, "throttle_fill_col", float4(0.2f, 0.45f, 0.15f, 0.6f)));
		m_renderTarget->FillGeometry(throttleFillPath.Get(), m_brush.Get());
		m_brush->SetColor(g_cfg.getFloat4(m_name, "brake_fill_col", float4(0.46f, 0.01f, 0.06f, 0.6f)));
		m_renderTarget->FillGeometry(brakeFillPath.Get(), m_brush.Get());
		m_brush->SetColor(g_cfg.getFloat4(m_name, "throttle_col", float4(0.38f, 0.91f, 0.31f, 0.8f)));
		m_renderTarget->DrawGeometry(throttleLinePath.Get(), m_brush.Get(), thickness);
		m_brush->SetColor(g_cfg.getFloat4(m_name, "brake_col", float4(0.93f, 0.03f, 0.13f, 0.8f)));
		m_renderTarget->DrawGeometry(brakeLinePath.Get(), m_brush.Get(), thickness);
      m_brush->SetColor(g_cfg.getFloat4(m_name, "abs_overlay_col", float4(1.0f, 0.6f, 0.0f, 1.0f)));
		m_renderTarget->DrawGeometry(absOverlayPath.Get(), m_brush.Get(), thickness + 1.0f);

		// Draw delta if enabled
		if (m_showDelta)
		{
			// positive deltas (red)
			m_brush->SetColor(g_cfg.getFloat4(m_name, "delta_pos_col", float4(0.9f, 0.1f, 0.1f, 0.9f)));
			m_renderTarget->DrawGeometry(deltaPosPath.Get(), m_brush.Get(), thickness);
			// negative deltas (green)
			m_brush->SetColor(g_cfg.getFloat4(m_name, "delta_neg_col", float4(0.1f, 0.9f, 0.2f, 0.9f)));
			m_renderTarget->DrawGeometry(deltaNegPath.Get(), m_brush.Get(), thickness);
		}
		m_brush->SetColor(g_cfg.getFloat4(m_name, "steering_col", float4(1, 1, 1, 0.3f)));
		m_renderTarget->DrawGeometry(steeringLinePath.Get(), m_brush.Get(), thickness);
		m_renderTarget->EndDraw();
	}

public:
	void setShowDelta(bool v) { m_showDelta = v; }
	bool isShowDelta() const { return m_showDelta; }

protected:
	std::vector<float2> m_throttleVtx;
	std::vector<float2> m_brakeVtx;
	std::vector<float2> m_steerVtx;
	std::vector<bool> m_absActiveVtx;
 std::vector<float2> m_deltaVtx;
	int m_currentSectorIdx = -1;
	float m_sectorStartDelta = 0.0f;
	bool m_showDelta = true;
	bool m_sectorStartValid = false;
};
