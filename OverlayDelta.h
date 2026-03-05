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

#include <cmath>
#include "Overlay.h"
#include "iracing.h"
#include "Config.h"

class OverlayDelta : public Overlay
{
public:
    OverlayDelta()
        : Overlay("OverlayDelta")
    {
    }

protected:
    virtual float2 getDefaultSize()
    {
        return float2(220, 80);
    }

    virtual void onEnable()
    {
        onConfigChanged();
    }

    virtual void onDisable()
    {
        m_text.reset();
    }

    virtual void onConfigChanged()
    {
        m_text.reset(m_dwriteFactory.Get());

        const std::string font = g_cfg.getString(m_name, "font", "Arial");
        const float fontSize = g_cfg.getFloat(m_name, "font_size", 32.0f);
        const int fontWeight = g_cfg.getInt(m_name, "font_weight", 700);

        HRCHECK(m_dwriteFactory->CreateTextFormat(
            toWide(font).c_str(),
            NULL,
            (DWRITE_FONT_WEIGHT)fontWeight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"en-us",
            &m_textFormat));
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    virtual void onUpdate()
    {
        const bool isValid = ir_LapDeltaToOptimalLap_OK.getBool();
        const float delta = ir_LapDeltaToOptimalLap.getFloat();
        const float zeroEps = g_cfg.getFloat(m_name, "zero_epsilon", 0.0005f);

        const float4 negativeCol = g_cfg.getFloat4(m_name, "negative_col", float4(0.20f, 0.90f, 0.20f, 1.0f));
        const float4 zeroCol = g_cfg.getFloat4(m_name, "zero_col", float4(1.0f, 1.0f, 1.0f, 1.0f));
        const float4 positiveCol = g_cfg.getFloat4(m_name, "positive_col", float4(0.95f, 0.20f, 0.20f, 1.0f));
        const float4 invalidCol = g_cfg.getFloat4(m_name, "invalid_col", float4(1.0f, 1.0f, 1.0f, 0.50f));

        float4 textCol = invalidCol;
        if (isValid)
        {
            if (fabsf(delta) <= zeroEps)
                textCol = zeroCol;
            else if (delta < 0)
                textCol = negativeCol;
            else
                textCol = positiveCol;
        }

         
        wchar_t value[64] = {};
        
        if (!isValid)
            swprintf(value, _countof(value), L"-.--");
        else if (fabsf(delta) <= zeroEps)
            swprintf(value, _countof(value), L"0.00");
        else
            swprintf(value, _countof(value), L"%+.2f", delta);

        m_renderTarget->BeginDraw();
        m_brush->SetColor(textCol);
        m_text.render(
            m_renderTarget.Get(),
            value,
            m_textFormat.Get(),
            0,
            (float)m_width,
            (float)m_height * 0.5f,
            m_brush.Get(),
            DWRITE_TEXT_ALIGNMENT_CENTER);
        m_renderTarget->EndDraw();
    }

protected:
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    TextCache m_text;
};
