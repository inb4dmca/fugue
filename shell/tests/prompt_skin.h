// The skin prompt_test.cpp builds its window from (a header so moc never lexes the raw string): MAP + INPUT + INFO.
#pragma once
inline const char* kPromptSkin = R"SKIN(macro "macro"
	elem "paste"
		name = "CTRL+V"
		command = "pastemacro"

window "default"
	elem "default"
		type = MAIN
		pos = 0,0
		size = 640x480
		is-default = true
	elem "map"
		type = MAP
		pos = 0,0
		size = 640x300
		anchor1 = 0,0
		anchor2 = 100,62
		is-default = true
	elem "input"
		type = INPUT
		pos = 0,300
		size = 640x24
		anchor1 = 0,62
		anchor2 = 100,67
	elem "info"
		type = INFO
		pos = 0,330
		size = 640x150
		anchor1 = 0,69
		anchor2 = 100,100
)SKIN";
