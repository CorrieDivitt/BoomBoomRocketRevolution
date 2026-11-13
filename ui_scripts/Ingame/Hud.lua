-- Boom Boom Rocket Revolution: the game's own HUD plus the hit timing readout (see Options\BBRR_hit.lua).
System.LoadLuaScript("orig:Ingame\\Hud.lua")
System.LoadLuaScript("Options\\BBRR_hit.lua")
-- Autoplay presses buttons on the controller that plays the song (src/autoplay.cpp).
pcall(gcinfo, "bbrr.autoplay.pad", tostring(UIGlobals and UIGlobals.primary_pad or 0))
