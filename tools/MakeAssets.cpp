// Renders the brand assets with the very same drawing code the app uses (logo mark, wordmark, aurora).
//   AuralloyAssets <output folder>
#include "../src/app/Theme.h"

using namespace juce;
using namespace al;

static void save (const Image& img, const File& f)
{
    f.deleteFile();
    FileOutputStream fos (f);
    PNGImageFormat png;
    png.writeImageToStream (img, fos);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File dir (argc > 1 ? argv[1] : ".");
    dir.createDirectory();

    // ---- app icon: squircle, aurora, the mark ----
    {
        const int S = 512;
        Image img (Image::ARGB, S, S, true, SoftwareImageType());
        Graphics g (img);
        Path sq;
        sq.addRoundedRectangle (14.0f, 14.0f, (float) S - 28.0f, (float) S - 28.0f, 118.0f);
        Path shadow (sq);
        g.setColour (Colour (0x55000000));
        g.fillPath (shadow, AffineTransform::translation (0.0f, 6.0f));
        g.saveState();
        g.reduceClipRegion (sq);
        drawAurora (g, Rectangle<float> (0, 0, (float) S, (float) S), 33.0, 0.25f);
        g.setGradientFill (ColourGradient (Colour (0x00000000), 256.0f, 190.0f, Colour (0x99000000), 256.0f, 520.0f, true));
        g.fillRect (0, 0, S, S);
        LogoState ls; ls.glow = 0.55f;
        drawLogoMark (g, Rectangle<float> (76.0f, 70.0f, 360.0f, 360.0f), ls);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0.0f, 14.0f, Colours::white.withAlpha (0.0f), 0.0f, 210.0f, false));
        g.fillRect (0, 0, S, 230);
        g.restoreState();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), 14.0f, 14.0f, Colours::white.withAlpha (0.08f), (float) S, (float) S, false));
        g.strokePath (sq, PathStrokeType (2.5f));
        save (img, dir.getChildFile ("icon.png"));
    }

    // ---- transparent logo mark ----
    {
        Image img (Image::ARGB, 512, 512, true, SoftwareImageType());
        Graphics g (img);
        LogoState ls; ls.glow = 0.0f;
        drawLogoMark (g, Rectangle<float> (36.0f, 36.0f, 440.0f, 440.0f), ls);
        save (img, dir.getChildFile ("logo-mark.png"));
    }

    // ---- logo with wordmark (transparent) ----
    {
        Image img (Image::ARGB, 1400, 380, true, SoftwareImageType());
        Graphics g (img);
        LogoState ls; ls.glow = 0.0f;
        drawLogoMark (g, Rectangle<float> (10.0f, 10.0f, 360.0f, 360.0f), ls);
        const float wh = 96.0f, ww = wordmarkWidth (wh);
        drawWordmark (g, Rectangle<float> (400.0f, 100.0f, ww, wh), 1.0f);
        g.setColour (col::text300);
        g.setFont (uiFont (30.0f, true));
        g.drawText ("TWO SONGS IN  -  ONE ALLOY OUT", Rectangle<float> (404.0f, 236.0f, 900.0f, 40.0f), Justification::centredLeft);
        save (img, dir.getChildFile ("logo-dark.png"));
    }

    // ---- social preview 1280 x 640 ----
    {
        Image img (Image::ARGB, 1280, 640, true, SoftwareImageType());
        Graphics g (img);
        drawAurora (g, Rectangle<float> (0, 0, 1280, 640), 40.0, 0.5f);
        g.setColour (Colour (0x55000000));
        g.fillRect (0, 0, 1280, 640);
        LogoState ls; ls.glow = 0.6f;
        drawLogoMark (g, Rectangle<float> (70.0f, 120.0f, 400.0f, 400.0f), ls);
        const float wh = 78.0f, ww = wordmarkWidth (wh);
        drawWordmark (g, Rectangle<float> (500.0f, 190.0f, ww, wh), 1.0f);
        g.setColour (col::text100);
        g.setFont (uiFont (36.0f, true));
        g.drawText ("Two songs in, one alloy out.", Rectangle<float> (500.0f, 300.0f, 740.0f, 50.0f), Justification::centredLeft);
        g.setColour (col::text300);
        g.setFont (uiFont (22.0f));
        g.drawFittedText ("Rule-based, explainable auto-mashups with an editable stem timeline, slowed + reverb and lofi - an open-source desktop studio.",
                          Rectangle<int> (500, 356, 700, 70), Justification::topLeft, 2);
        const char* chips[] = { "Auto-mashup", "Slowed + Reverb", "Timeline editor", "20 languages" };
        float x = 500.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float w = 34.0f + (float) String (chips[i]).length() * 10.0f;
            Rectangle<float> r (x, 470.0f, w, 44.0f);
            g.setColour (sourceColour (i).withAlpha (0.22f));
            g.fillRoundedRectangle (r, 22.0f);
            g.setColour (sourceColour (i).withAlpha (0.9f));
            g.drawRoundedRectangle (r, 22.0f, 1.5f);
            g.setColour (col::text100);
            g.setFont (uiFont (19.0f, true));
            g.drawText (chips[i], r, Justification::centred);
            x += w + 12.0f;
        }
        save (img, dir.getChildFile ("social-preview.png"));
    }
    return 0;
}
