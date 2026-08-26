# raylib

## Text Pipeline
Pipeline for high-fidelity, scale-independent UI text in raylib and raygui

**1. Load font as SDF, baked at high resolution**
```c
Font uiFont = { 0 };
uiFont.baseSize = 96; // bake large — SDF derives crispness from this at any render size
uiFont.glyphCount = 95;

// Load with SDF pixel type (raylib supports this via LoadFontEx internals,
// or manually via GenImageFontAtlas + FONT_SDF type):
int codepoints[95];
for (int i = 0; i < 95; i++) codepoints[i] = 32 + i; // ASCII range; extend for Unicode

uiFont = LoadFontEx("hack.ttf", 96, codepoints, 95);
SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
```

**2. Load the SDF shader**
```c
Shader sdfShader = LoadShader(0, "resources/shaders/sdf.fs");
```

**3. Register font with raygui — critical, easy to miss**
```c
GuiSetFont(uiFont);
GuiSetStyle(DEFAULT, TEXT_SIZE, 20);       // your default UI text size
GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, 24); // avoid cramped default
```

**4. Draw UI/dialogs inside the SDF shader block**
```c
BeginShaderMode(sdfShader);
    GuiWindowBox(dialogRect, "Passphrase");
    GuiLabel(labelRect, "Enter passphrase:");
    GuiTextBox(inputRect, buffer, bufSize, editMode);
EndShaderMode();
```
raygui draws through raylib's `DrawTextEx` internally, so as long as the SDF shader is active during the `Gui*` calls, all its text renders through the distance field — sharp at any runtime scale.

**5. Window/context flags for HiDPI + AA**
```c
SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);
InitWindow(width, height, "title");
```

**6. Scale UI layout with actual DPI, not hardcoded pixels**
```c
Vector2 dpiScale = GetWindowScaleDPI();
float uiScale = dpiScale.x; // usually equal to dpiScale.y
GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(20 * uiScale));
```

**7. Custom raygui style for polish (optional but high-leverage)**
```c
GuiLoadStyle("style_dark.rgs"); // or tweak individual style constants
GuiSetStyle(DEFAULT, BORDER_WIDTH, 2);
GuiSetStyle(DEFAULT, TEXT_PADDING, 8);
```

