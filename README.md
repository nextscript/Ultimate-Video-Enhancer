<img src="https://raw.githubusercontent.com/nextscript/Globale-Video-Filter-Overlay/refs/heads/main/preview.png">
<hr>
<img src="https://raw.githubusercontent.com/nextscript/Globale-Video-Filter-Overlay/refs/heads/main/before.png">
<img src="https://raw.githubusercontent.com/nextscript/Globale-Video-Filter-Overlay/refs/heads/main/after.png">
<hr>
<h2>What this script does</h2>
<hr>
This userscript adds a global video filter pipeline to all HTML5 "video" elements on any website you visit.
It applies visual enhancements like sharpen/blur, black & white level tuning, optional cinematic color looks, and an adjustable grain reduction / texture control.

It also renders a small on-video overlay UI (icons + sliders). The slider values are stored globally via Tampermonkey storage, so the same settings are used across all sites, all players, and all tabs.
<hr>
<h2>Key features</h2>
<hr>
<ul>
<li>Works on any site with HTML5 videos (streaming sites, embedded players, etc.)</li>
<li>Per-video overlay UI positioned on the video (top-right)</li>
<li>Global persistent settings (one set of values shared everywhere)</li>
<li>Sharpen OR Blur using one slider (negative = blur, positive = sharpen)</li>
<li>Black Level (BL) lift/crush blacks (snap-to-zero)</li>
<li>White Level (WL) highlight “knee” tuning to push/pull bright areas (snap-to-zero)</li>

<li>DN (DeNoise / Texture)</li>
<ul><li>Positive values: grain reduction / smoothing</li><li>Negative values: texture boost (adds micro-contrast detail)</li></ul>
<li>HDR (SDR→HDR look):</li>
<ul><li>Increases local contrast, punch, and highlight shaping</li></ul>
<li>Optional cinematic looks:</li>
<ul><li>Dark & Moody</li><li>Teal & Orange</li><li>Vibrant & Saturated</li></ul>
<li>Color Grading Profile</li>
<ul><li>Green = Movie, Blue = Anime, Red = Gaming, Yellow = EyeCare, White = User</li></ul>
<li>Grading Settings (G)</li>
<ul><li>Make your own Settings for User Profile</li></ul>
<li>Auto-Scene-Match </li>
<ul><li>The script continuously samples frames and gently auto-adjusts brightness/contrast/saturation/hue-rotate to keep exposure, contrast, saturation and color cast more stable/neutral</li></ul>
<hr>
<h2>Keyboard shortcuts (toggle on/off)</h2>
All shortcuts use: CTRL + ALT + key
<hr>
<ul>
<li>CTRL + ALT + B → Toggle Base Tone Chain (brightness/contrast/saturation layer)</li>
<li>CTRL + ALT + D → Toggle Dark & Moody</li>
<li>CTRL + ALT + O → Toggle Teal & Orange</li>
<li>CTRL + ALT + V → Toggle Vibrant & Saturated</li>
<li>CTRL + ALT + P → Toggle HDR</li>
<li>CTRL + ALT + C → Profile Cycle (Color Grading)</li>
<li>CTRL + ALT + G → Show/Hide Grading Settings (For User Profile) + Assistive-Recoloring Filters (Experimental) + LUT Profiles </li>
<li>CTRL + ALT + A → Toggle Auto-Scene-Match </li>
<li>CTRL + ALT + I → Show/Hide Export/Import JSON (Screenshot & Recording) + Debug ON/OFF </li>
<li>CTRL + ALT + S → Show/Hide Scopes HUD</li>
<li>CTRL + ALT + X → Toggle GPU Pipeline Mode </li>
<li>CTRL + ALT + H → Show/Hide the overlay UI (icons + sliders)</li>
<li>Shift + Q  → Switch profiles from Config menu </b></li>
<hr>
Notes:
<ul>
<li>The overlay is hidden by default. Press CTRL+ALT+H to display it.</li>
<li>The icons B/D/O/V reflect which modes are currently enabled.</li>
</ul>
<hr>
<h2>Sliders (all global + persistent)</h2>
All slider values are saved automatically and reused everywhere.
All sliders support Snap-to-0 around the center, so you can easily return to neutral.
<hr>
<b>SL — Sharpen/Blur Level (-2.0 … +2.0, step 0.1)</b>
<ul>
<li>< 0 = Blur</li>
<li>> 0 = Sharpen</li>
</ul>

<b>SR — Radius (-2.0 … +2.0, step 0.1)</b>
<ul>
<li>Controls how “wide” the sharpen/blur effect is</li>
<li>Higher = stronger radius / softer influence</li>
</ul>

<b>BL — Black Level (-2.0 … +2.0, step 0.1)</b>
<ul>
<li>Positive lifts blacks (brighter shadows)</li>
<li>Negative crushes blacks (deeper shadows)</li>
</ul>

<b>WL — White Level (-2.0 … +2.0, step 0.1)</b>
<ul>
<li>Positive pushes highlights brighter</li>
<li>Negative compresses highlights (more headroom, less clipping)</li>
</ul>

<b>DN — DeNoise / Texture (-1.5 … +1.5, step 0.1)</b>
<ul>
<li>Positive = grain reduction / smoothing</li>
<li>Negative = texture boost (micro-contrast sharpening)</li>
</ul>

<b>HDR — HDR on/off (-1.0 … +2.0, step 0.1) Default: +0.3</b>
<ul>
<li>Positive = add punch, clarity, deeper highlights, and richer colors</li>
<li>Negative = soften the image, reducing contrast for a smoother look</li>
</ul>
<ul>
<li>HDR completely reworked: Proper ACES tonemapping in linear color space</li>
<li>Significantly more natural highlights and richer colors</li>
<li>Smarter HDR control (up to +1.5 stops exposure)</li>
</ul>
<br>
<hr>
<h2>Profile Cycle</h2>
<hr>
<b>Off</b><br>
No profile filtering active. Only manual settings (Base, Dark&Moody, Teal&Orange, Vibrant, HDR, RGB Gain) are applied.<br>
<br>
<b>Movie</b><br>
Cinematic look with warm color grading and softer contrast. Optimized for movies and series with natural skin tone reproduction.<br>
<br>
<b>Anime</b><br>
Vibrant, colorful look specifically for anime and animations. Enhances typical anime colors and provides clearer lines.<br>
<br>
<b>Gaming</b><br>
High-contrast, intense look for games. Improves visibility in dark areas and enhances colors for a more immersive gaming experience.<br>
<br>
<b>EyeCare</b><br>
Reduces blue light by 50% for more comfortable viewing during long sessions, especially in the evening. Warmer color temperature similar to night mode features.<br>
<br>
<b>User</b><br>
Your own manual settings. All your personal adjustments from the Grading HUD (contrast, black level, white level, RGB gain, etc.) take effect here.<br>
<hr>
<h2>Auto-Scene-Match</h2>
<hr>
An intelligent automatic image optimization that adapts to your video content in real-time.

<b>How it works:</b>
The function continuously analyzes the playing video image and calculates optimal values for brightness, contrast, saturation, and color cast. These values are smoothly and fluidly adjusted to the current scene.

<b>What is automatically controlled:</b>
<ul>
<li>Brightness – Adjusts overall brightness to an optimal level</li>
<li>Contrast – Optimizes dynamic range for more depth in the image</li>
<li>Saturation – Brings out colors naturally without oversaturation</li>
<li>Color Cast – Automatically corrects color tints (optional feature)</li>
</ul>
<br>
<b>Special features:</b>
<ul>
<li>Responds to scene changes with faster adaptation</li>
<li>Takes motion into account for more stable analysis</li>
<li>Adaptive algorithm glides smoothly between values (no hard switching)</li>
<li>Cannot analyze DRM-protected videos (uses last good values)</li>
</ul>
<br>
<b>Visual feedback:</b><br>
A small dot in the video shows the status(If debug=true Default: false):<br>

🟢 Green = Active, no changes<br>
🟢 Light Green = Active and currently adjusting<br>
🔴 Red = No updates (e.g., with DRM)
<br>
<b>Goal:</b><br>
Always optimally looking videos without manual intervention – especially useful for changing scenes or different video sources.
<hr>
<h2>Scopes HUD</h2>
<hr>
A professional analysis tool directly in the video that helps you make precise color corrections.<br>

<b>Luma Histogram (Y)</b><br>
Displays the brightness distribution in the image from black (left) to white (right). Ideal for checking contrast, exposure, and avoiding clipping in highlights or shadows.<br>

<b>RGB Parade</b><br>
Separate histograms for Red, Green, and Blue side by side. Enables precise analysis of color balance and assists with color correction and white balance.<br>

<b>Saturation Meter</b><br>
Live display of the average color saturation in the image. Helps detect excessive or insufficient saturation.<br>

<b>Average Values</b><br>
Shows the mean values for brightness (Y), RGB average, and saturation as numerical readouts.<br>

<b>Special Feature:</b><br>
The Scopes HUD displays values after applying all your filters – so you see exactly what your settings are doing.
<hr>
<h2>GPU Pipeline Mode</h2>
<hr>
The GPU Pipeline Mode is an alternative rendering mode for maximum performance. Instead of using SVG filters, all image enhancements are processed directly on the GPU using WebGL/Canvas.
<br>
<b>Benefits:</b>
<ul>
<li>Maximum Speed: Up to 3x faster than SVG mode</li>
<li>Lower Memory Usage: Optimized for weaker devices</li>
<li>4K-ready: Ideal for high-resolution videos</li>
<li>Full Functionality: All filters, profiles, and RGB gain controls remain available</li>
<li>Real-time Processing: Every frame is optimized at pixel level</li>
</ul>
<b>Perfect for:</b>
<ul>
<li>Older computers and laptops</li>
<li>4K/8K videos</li>
<li>Browsers with slow SVG implementation</li>
<li>Maximum performance with minimal quality loss</li>
</ul>
<hr>
<h2>Assistive-Recoloring Filters (Experimental)</h2>
<hr>
<b>These filters help people with color blindness distinguish colors better:</b>
<ul>
<li>Protanopia (Red-Blind): Enhances red-cyan contrast and shifts red tones to more perceptible orange hues</li>
<li>Deuteranopia (Green-Blind): Enhances green-magenta contrast and shifts green tones to more distinguishable blue hues</li>
<li>Tritanopia (Blue-Blind): Enhances blue-yellow contrast and optimizes blue tones for better recognition</li>
</ul>
<hr>
<h2>LUT Filter Feature </h2>
<hr>
<b>LUT Filter System:</b>
The system converts a tiled 2D LUT PNG (e.g. 512×512) and 3D LUT .cube into a 4×5 row-major color matrix using least-squares approximation.
The resulting matrix is applied in real-time via feColorMatrix, enabling instant color transformation without page reload.

<b>LUT Profile Manager:</b>
<ul>
<li>Supports tiled 2D LUT PNGs (512×512) and 3D LUT .cube files</li>
<li>Create, edit, delete</li>
<li>Export all profiles as ZIP</li>
<li>Import JSON or ZIP (same-name overwrite)</li>
</ul>
<hr>
<b>LUTs Profiles:</b><br>
<a href="https://github.com/nextscript/Globale-Video-Filter-Overlay/raw/refs/heads/main/LUTs1.7.3_v1.1.zip" target="_blank">LUTs Profiles Examples</a>
<hr>
<h2>⚙️ Config-Button</b></h2>
<hr>
A button in the IO-HUD (which you open with Ctrl+Alt+I) that opens the Profile Manager menu. There you can:
<ul>
<li>Create and manage multiple user profiles</li>
<li>Switch between different profile settings</li>
<li>Name, save and delete profiles</li>
<li>See the active profile at a glance</li>
</ul>
<b>Only one profile can be active at a time!</b>
<hr>
<h2>On-Screen Notification:</h2>
<b>A brief overlay in the top-left corner of the video that shows which profile is currently active for 3 seconds.(Shift + Q)</b>
<hr>
<h2>Edge Detection <b style="color:red;">(New)</b></h2>
Edge detection finds the edges and lines in an image or video.
It looks for places where brightness or colors change strongly.
<br>
<b>What it is useful for:</b><br>
<ul>
<li>Showing clear outlines</li>
<li>Making details stand out</li>
<li>Making videos look clearer and sharper</li>
<li>Improving the look of compressed streams</li>
</ul>
<br>
<b>Note:</b><br>
The edge value can be changed in the Ctrl + Alt + I menu inside the JSON settings.
After pressing Save, the filter updates immediately and the change becomes visible in the video.
<hr>
<ul>
<li>0.01 = almost invisible</li>
<li>0.05–0.15 = light</li>
<li>0.20–0.35 = normal</li>
<li>0.50+ = strong</li>
</ul>
<hr>
<h2>Recommended starting presets</h2>
<hr>
<b>Clean / natural</b>
<ul>
<li>SL: +1.0</li>
<li>SR: +0.6</li>
<li>BL: -1.3</li>
<li>WL: 0.3</li>
<li>DN: -0.8</li>
<li>HDR: 0.0</li>
</ul>
<b>Stronger sharpness</b>
<ul>
<li>SL: +1.2</li>
<li>SR: +1.2</li>
<li>BL: -0.2</li>
<li>WL: -0.1</li>
<li>DN: 0.0</li>
</ul>
<hr>
<h2>My Profile:</h2>
<hr>
<b>SVG Mode (Recommended)</b><br>
<a href="https://raw.githubusercontent.com/nextscript/Globale-Video-Filter-Overlay/refs/heads/main/My_Profile.json" target="_blank">My_Profile.json</a><br>
+ LUT : <a href="https://github.com/nextscript/Globale-Video-Filter-Overlay/raw/refs/heads/main/Cinematic%201.zip" target="_blank">Cinematic 1.zip</a><br>
<b>GPU Mode</b><br>
<a href="https://raw.githubusercontent.com/nextscript/Globale-Video-Filter-Overlay/refs/heads/main/GPU_Mode_Profile.json" target="_blank">GPU_Mode_Profile.json</a><br>
<hr>
<h2>IIMPORTANT</h2>
<hr>
<b>Chrome/Edge 138+:</b>
<ul>
<li>1. Enable Developer Mode at chrome://extensions.</li>
<li>2. Enable “Allow User Scripts” in Tampermonkey extension settings.</li>
<li>3. Restart Browser.</li>
</ul>
<b>More Info:</b><br>
<a href="https://www.tampermonkey.net/faq.php?locale=en#Q209" target="_blank">Permission to execute userscripts</a>
