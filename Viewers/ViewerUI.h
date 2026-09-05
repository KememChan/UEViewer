#ifndef __VIEWER_UI_H__
#define __VIEWER_UI_H__

#if RENDERING

#include "Core.h"

struct SDL_Window;
union SDL_Event;
class CObjectViewer;

namespace ViewerUI
{
	void Init(SDL_Window* window);
	void Shutdown();
	bool ProcessEvent(const SDL_Event* evt);
	void NewFrame();
	void Draw(CObjectViewer* viewer);
	void Render();

	void ToggleVisible();
	bool IsVisible();
	void SetVisible(bool bVisible);

	bool WantCaptureMouse();
	bool WantCaptureKeyboard();
}

#endif // RENDERING

#endif // __VIEWER_UI_H__
