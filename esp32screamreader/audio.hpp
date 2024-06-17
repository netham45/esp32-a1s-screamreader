#pragma once
void process_audio_actions(bool is_startup);
void register_button(int button,void (*action)(bool, int, void *));
void pcm_handler();
void setup_audio();
