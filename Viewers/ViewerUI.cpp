#include "Core.h"
#include "UnCore.h"

#if RENDERING

#include "UnObject.h"
#include "ViewerUI.h"
#include "ObjectViewer.h"
#include "GlWindow.h"
#include "CoreGL.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "MeshInstance/MeshInstance.h"
#include "UnrealMaterial/UnMaterial.h"

#include <SDL2/SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl2.h"

namespace ViewerUI
{

static bool bInitialized = false;
static bool bShowUI = true;
static bool bShowInspector = true;
static bool bShowAnimPanel = true;

// Filter state for animations
static char searchAnimFilter[128] = "";
static int  animFilterType = 0; // 0 = All, 1 = Standard (Non-Additive), 2 = Additive Only
static bool bAnimLoop = true;
static float animSpeed = 1.0f;
static bool bAnimPlaying = true;

// Export status notification
static char exportStatusMsg[128] = "";
static unsigned exportStatusTimer = 0;

// Active viewer reference and play/pause helper
static CObjectViewer* GCurrentViewer = NULL;

static void TogglePlayPause()
{
	CSkelMeshViewer* skelViewer = GCurrentViewer ? GCurrentViewer->AsSkelMeshViewer() : NULL;
	if (!skelViewer) return;

	CSkelMeshInstance* meshInst = skelViewer->GetSkelInst();
	if (!meshInst) return;

	bool bPlaying = (meshInst->GetAnimRate(0) != 0.0f);
	if (bPlaying)
	{
		skelViewer->PauseCurrentAnim();
		bAnimPlaying = false;
	}
	else
	{
		skelViewer->PlayCurrentAnim(bAnimLoop, animSpeed);
		bAnimPlaying = true;
	}
}

static void SetupImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding    = 6.0f;
	style.ChildRounding     = 4.0f;
	style.FrameRounding     = 4.0f;
	style.PopupRounding     = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding      = 3.0f;
	style.TabRounding       = 4.0f;

	style.WindowBorderSize  = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.PopupBorderSize   = 1.0f;

	style.WindowPadding     = ImVec2(10, 10);
	style.FramePadding      = ImVec2(6, 4);
	style.ItemSpacing       = ImVec2(8, 6);
	style.ItemInnerSpacing  = ImVec2(6, 4);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
	colors[ImGuiCol_WindowBg]              = ImVec4(0.09f, 0.10f, 0.12f, 0.94f);
	colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.13f, 0.16f, 0.80f);
	colors[ImGuiCol_PopupBg]               = ImVec4(0.11f, 0.12f, 0.15f, 0.98f);
	colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.27f, 0.33f, 0.70f);
	colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.25f, 0.31f, 1.00f);
	colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.30f, 0.38f, 1.00f);
	colors[ImGuiCol_TitleBg]               = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
	colors[ImGuiCol_TitleBgActive]         = ImVec4(0.17f, 0.20f, 0.26f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.10f, 0.11f, 0.13f, 0.75f);
	colors[ImGuiCol_MenuBarBg]             = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
	colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.11f, 0.13f, 0.60f);
	colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.24f, 0.27f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.32f, 0.36f, 0.44f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);
	colors[ImGuiCol_CheckMark]             = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab]            = ImVec4(0.28f, 0.58f, 0.92f, 1.00f);
	colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.38f, 0.68f, 1.00f, 1.00f);
	colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
	colors[ImGuiCol_ButtonHovered]         = ImVec4(0.26f, 0.34f, 0.46f, 1.00f);
	colors[ImGuiCol_ButtonActive]          = ImVec4(0.22f, 0.45f, 0.75f, 1.00f);
	colors[ImGuiCol_Header]                = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
	colors[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.35f, 0.48f, 1.00f);
	colors[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.44f, 0.72f, 1.00f);
	colors[ImGuiCol_Separator]             = ImVec4(0.22f, 0.25f, 0.30f, 0.80f);
	colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.32f, 0.45f, 0.65f, 1.00f);
	colors[ImGuiCol_SeparatorActive]       = ImVec4(0.35f, 0.55f, 0.85f, 1.00f);
	colors[ImGuiCol_ResizeGrip]            = ImVec4(0.24f, 0.27f, 0.33f, 0.50f);
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.32f, 0.45f, 0.65f, 0.75f);
	colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.35f, 0.55f, 0.85f, 1.00f);
	colors[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
	colors[ImGuiCol_TabHovered]            = ImVec4(0.26f, 0.35f, 0.48f, 1.00f);
	colors[ImGuiCol_TabActive]             = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);
	colors[ImGuiCol_TabUnfocused]          = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.16f, 0.22f, 0.32f, 1.00f);
	colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.24f, 0.27f, 0.33f, 1.00f);
	colors[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
	colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
}

void Init(SDL_Window* window)
{
	if (bInitialized) return;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	SetupImGuiStyle();

	ImGui_ImplSDL2_InitForOpenGL(window, NULL);
	ImGui_ImplOpenGL2_Init();

	bInitialized = true;
}

void Shutdown()
{
	if (!bInitialized) return;

	GCurrentViewer = NULL;
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	bInitialized = false;
}

bool ProcessEvent(const SDL_Event* evt)
{
	if (!bInitialized) return false;

	// Toggle UI shortcut: F1 or ` (Tilde)
	if (evt->type == SDL_KEYDOWN)
	{
		if (evt->key.keysym.sym == SDLK_F1 || evt->key.keysym.sym == SDLK_BACKQUOTE)
		{
			ToggleVisible();
			return true;
		}
	}

	ImGuiIO& io = ImGui::GetIO();

	// Play / Pause shortcut: Space bar (when not typing in an active text box)
	if (evt->type == SDL_KEYDOWN && evt->key.keysym.sym == SDLK_SPACE && !evt->key.repeat)
	{
		if (!io.WantTextInput)
		{
			TogglePlayPause();
			return true;
		}
	}
	if (evt->type == SDL_KEYUP && evt->key.keysym.sym == SDLK_SPACE)
	{
		if (!io.WantTextInput)
		{
			return true;
		}
	}

	ImGui_ImplSDL2_ProcessEvent(evt);

	if (evt->type == SDL_MOUSEBUTTONUP && Viewport::MouseButtons != 0)
	{
		return false;
	}
	if (io.WantCaptureMouse && (evt->type == SDL_MOUSEMOTION || evt->type == SDL_MOUSEBUTTONDOWN || evt->type == SDL_MOUSEBUTTONUP || evt->type == SDL_MOUSEWHEEL))
	{
		return true;
	}
	if (bShowUI && io.WantCaptureKeyboard && (evt->type == SDL_KEYDOWN || evt->type == SDL_KEYUP))
	{
		return true;
	}

	return false;
}

void NewFrame()
{
	if (!bInitialized) return;

	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
}

void ToggleVisible()
{
	bShowUI = !bShowUI;
}

bool IsVisible()
{
	return bShowUI;
}

void SetVisible(bool bVisible)
{
	bShowUI = bVisible;
}

bool WantCaptureMouse()
{
	if (!bInitialized || !bShowUI) return false;
	return ImGui::GetIO().WantCaptureMouse;
}

bool WantCaptureKeyboard()
{
	if (!bInitialized || !bShowUI) return false;
	return ImGui::GetIO().WantCaptureKeyboard;
}

// ----------------------------------------------------------------------------
// UI Rendering Sections
// ----------------------------------------------------------------------------

static void DrawMainMenuBar(CObjectViewer* viewer)
{
	if (ImGui::BeginMainMenuBar())
	{
		const char* objName = (viewer && viewer->Object) ? viewer->Object->Name : "None";
		const char* className = (viewer && viewer->Object) ? viewer->Object->GetClassName() : "";

		ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "UEViewer");
		ImGui::Separator();

		ImGui::Text("%s: %s", className, objName);
		ImGui::Separator();

		if (ImGui::BeginMenu("Panels"))
		{
			ImGui::MenuItem("Inspector", NULL, &bShowInspector);
			ImGui::MenuItem("Animation Studio", NULL, &bShowAnimPanel);
			ImGui::Separator();
			ImGui::MenuItem("Show Legacy HUD / Info", "Ctrl+Q", &GShowDebugInfo);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Camera"))
		{
			bool bFree = (GCameraMode == CAMERA_MODE_FREE);
			bool bOrbit = (GCameraMode == CAMERA_MODE_ORBIT_OBJECT);
			if (ImGui::MenuItem("Free Camera", "Ctrl+F", bFree))
				SetCameraMode(CAMERA_MODE_FREE);
			if (ImGui::MenuItem("Orbit & Follow Object (Blender)", "Ctrl+F", bOrbit))
				SetCameraMode(CAMERA_MODE_ORBIT_OBJECT);
			ImGui::Separator();
			if (ImGui::MenuItem("Focus on Object", "F"))
			{
				if (viewer) FocusCameraOnPoint(viewer->GetObjectOrigin());
			}
			if (ImGui::MenuItem("Reset View", "R"))
				ResetView();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Lighting"))
		{
			static const char* const modes[] = { "Anime Unlit (Pure)", "Studio HDRI", "Outdoor Sunlight", "Headlamp" };
			for (int m = 0; m < LIGHTING_LAST; m++)
			{
				bool bSelected = (GLightingMode == m);
				if (ImGui::MenuItem(modes[m], NULL, bSelected))
					GLightingMode = m;
			}
			ImGui::Separator();
			ImGui::SliderFloat("Intensity", &GLightIntensity, 0.0f, 3.0f, "%.2fx");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Export"))
		{
			if (ImGui::MenuItem("Export Current Object"))
			{
				if (viewer) viewer->Export();
			}
			CSkelMeshViewer* skelViewer = viewer ? viewer->AsSkelMeshViewer() : NULL;
			if (skelViewer && skelViewer->AnimIndex >= 0)
			{
				const CAnimSet* animSet = skelViewer->GetActiveAnimSet();
				if (animSet && skelViewer->AnimIndex < animSet->Sequences.Num())
				{
					char animExportLabel[128];
					appSprintf(ARRAY_ARG(animExportLabel), "Export Selected Anim: %s", *animSet->Sequences[skelViewer->AnimIndex]->Name);
					if (ImGui::MenuItem(animExportLabel))
					{
						skelViewer->ExportAnimation(skelViewer->AnimIndex);
						appSprintf(ARRAY_ARG(exportStatusMsg), "Exported: %s", *animSet->Sequences[skelViewer->AnimIndex]->Name);
						exportStatusTimer = SDL_GetTicks() + 4000;
					}
				}
			}
			ImGui::EndMenu();
		}

		// Right aligned items
		float rightOffset = 220.0f;
		if (ImGui::GetWindowWidth() > rightOffset)
		{
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - rightOffset);
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%.0f FPS", ImGui::GetIO().Framerate);
			ImGui::SameLine();
			if (ImGui::SmallButton(bShowUI ? "Hide UI (F1)" : "Show UI (F1)"))
			{
				ToggleVisible();
			}
		}

		ImGui::EndMainMenuBar();
	}
}

static void DrawInspectorPanel(CObjectViewer* viewer)
{
	if (!bShowInspector) return;

	ImGui::SetNextWindowSize(ImVec2(340, 500), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(16, 40), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Model & Viewport Inspector", &bShowInspector))
	{
		CMeshViewer* meshViewer = viewer ? viewer->AsMeshViewer() : NULL;
		CSkelMeshViewer* skelViewer = viewer ? viewer->AsSkelMeshViewer() : NULL;
		CStatMeshViewer* statViewer = viewer ? viewer->AsStatMeshViewer() : NULL;

		if (ImGui::CollapsingHeader("Camera & View", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const char* cameraModes[] = { "Free Camera", "Orbit Object (Blender style)" };
			int currentCam = GCameraMode;
			if (ImGui::Combo("Camera Mode", &currentCam, cameraModes, IM_ARRAYSIZE(cameraModes)))
			{
				SetCameraMode(currentCam);
			}

			float fov = GetCameraFOV();
			if (ImGui::SliderFloat("Field of View", &fov, 10.0f, 120.0f, "%.0f deg"))
			{
				SetCameraFOV(fov);
			}

			float dist = GetCameraDistance();
			if (ImGui::SliderFloat("Distance", &dist, 25.0f, 2048.0f, "%.0f"))
			{
				SetCameraDistance(dist);
			}

			if (ImGui::Button("Focus Camera (F)", ImVec2(140, 0)))
			{
				if (viewer) FocusCameraOnPoint(viewer->GetObjectOrigin());
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset View (R)", ImVec2(140, 0)))
			{
				ResetView();
			}
		}

		if (ImGui::CollapsingHeader("Lighting Setup", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const char* lightModes[] = { "Anime Unlit (Pure)", "Studio HDRI", "Outdoor Sunlight", "Headlamp" };
			int currentLight = GLightingMode;
			if (ImGui::Combo("Preset", &currentLight, lightModes, IM_ARRAYSIZE(lightModes)))
			{
				GLightingMode = currentLight;
			}

			ImGui::SliderFloat("Brightness", &GLightIntensity, 0.0f, 3.0f, "%.2fx");
			ImGui::SliderFloat("Orbit Yaw", &GLightYaw, 0.0f, 360.0f, "%.0f deg");
			ImGui::SliderFloat("Orbit Pitch", &GLightPitch, -85.0f, 85.0f, "%.0f deg");
		}

		if (meshViewer)
		{
			if (ImGui::CollapsingHeader("Mesh Display", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Wireframe (W)", &meshViewer->Wireframe);

				CMeshInstance* inst = meshViewer->GetMeshInst();
				if (skelViewer && skelViewer->GetMesh())
				{
					CSkelMeshInstance* skelInst = skelViewer->GetSkelInst();
					CSkeletalMesh* skelMesh = skelViewer->GetMesh();
					int numLods = skelMesh->Lods.Num();
					if (skelInst && numLods > 1)
					{
						int currentLod = skelInst->LodIndex;
						if (currentLod < 0) currentLod = 0;
						if (ImGui::SliderInt("LOD Level (L)", &currentLod, 0, numLods - 1))
						{
							skelInst->LodIndex = currentLod;
						}
					}

					int lodIdx = (skelInst && skelInst->LodIndex >= 0 && skelInst->LodIndex < numLods) ? skelInst->LodIndex : 0;
					int numUV = (skelInst && numLods > 0) ? skelMesh->Lods[lodIdx].NumTexCoords : 1;
					if (skelInst && numUV > 1)
					{
						int currentUV = skelInst->UVIndex;
						if (ImGui::SliderInt("UV Channel (U)", &currentUV, 0, numUV - 1))
						{
							skelInst->UVIndex = currentUV;
						}
					}
				}

				if (skelViewer)
				{
					const char* skelModes[] = { "Hide Skeleton", "Mesh + Bones", "Bones Only" };
					ImGui::Combo("Skeleton (S)", &skelViewer->ShowSkel, skelModes, IM_ARRAYSIZE(skelModes));

					ImGui::Checkbox("Show Bone Labels (B)", &skelViewer->ShowLabels);
					ImGui::Checkbox("Show Attachments (A)", &skelViewer->ShowAttach);

					bool bShowInf = (meshViewer->DrawFlags & DF_SHOW_INFLUENCES) != 0;
					if (ImGui::Checkbox("Show Vertex Weights (I)", &bShowInf))
					{
						if (bShowInf) meshViewer->DrawFlags |= DF_SHOW_INFLUENCES;
						else          meshViewer->DrawFlags &= ~DF_SHOW_INFLUENCES;
					}

					if (ImGui::Button("Dump Bones to Log"))
					{
						CSkelMeshInstance* skelInst = skelViewer->GetSkelInst();
						if (skelInst) skelInst->DumpBones();
					}
				}
			}

			if (ImGui::CollapsingHeader("Materials"))
			{
				if (skelViewer && skelViewer->GetMesh())
				{
					CSkelMeshInstance* skelInst = skelViewer->GetSkelInst();
					CSkeletalMesh* skelMesh = skelViewer->GetMesh();
					if (skelInst && skelMesh && skelMesh->Lods.Num() > 0)
					{
						int lodIdx = (skelInst->LodIndex >= 0 && skelInst->LodIndex < skelMesh->Lods.Num()) ? skelInst->LodIndex : 0;
						const CSkelMeshLod& lod = skelMesh->Lods[lodIdx];
						if (ImGui::BeginTable("MaterialsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
						{
							ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 40.0f);
							ImGui::TableSetupColumn("Material Name", ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableSetupColumn("Faces", ImGuiTableColumnFlags_WidthFixed, 60.0f);
							ImGui::TableHeadersRow();

							for (int s = 0; s < lod.Sections.Num(); s++)
							{
								const CMeshSection& sec = lod.Sections[s];
								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								ImGui::Text("%d", s);
								ImGui::TableNextColumn();
								ImGui::Text("%s", sec.Material ? sec.Material->Name : "None");
								ImGui::TableNextColumn();
								ImGui::Text("%d", sec.NumFaces);
							}
							ImGui::EndTable();
						}
					}
				}
			}
		}

		if (statViewer && statViewer->GetStaticMesh())
		{
			CStaticMesh* sm = statViewer->GetStaticMesh();
			if (ImGui::CollapsingHeader("Static Mesh Stats", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("LODs: %d", sm->Lods.Num());
				for (int l = 0; l < sm->Lods.Num(); l++)
				{
					const CStaticMeshLod& lod = sm->Lods[l];
					ImGui::BulletText("LOD %d: %d verts, %d sections", l, lod.NumVerts, lod.Sections.Num());
				}
			}
		}
	}
	ImGui::End();
}

static void DrawAnimationStudio(CSkelMeshViewer* skelViewer)
{
	if (!skelViewer || !bShowAnimPanel) return;

	CSkelMeshInstance* meshInst = skelViewer->GetSkelInst();
	if (!meshInst) return;

	const CAnimSet* animSet = meshInst->GetAnim();
	if (!animSet) return;

	int totalAnims = animSet->Sequences.Num();
	if (totalAnims <= 0) return;

	ImGui::SetNextWindowSize(ImVec2(720, 400), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(370, 420), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Animation Studio", &bShowAnimPanel))
	{
		// Top Filter Bar
		ImGui::Text("Filter:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputTextWithHint("##Search", "Search animation...", searchAnimFilter, sizeof(searchAnimFilter));
		ImGui::SameLine();

		// Category filter buttons
		if (ImGui::RadioButton("All", animFilterType == 0)) animFilterType = 0;
		ImGui::SameLine();
		if (ImGui::RadioButton("Standard", animFilterType == 1)) animFilterType = 1;
		ImGui::SameLine();
		if (ImGui::RadioButton("Additive", animFilterType == 2)) animFilterType = 2;

		if (exportStatusMsg[0] != '\0' && SDL_GetTicks() < exportStatusTimer)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.3f, 1.0f), "  [OK] %s", exportStatusMsg);
		}

		ImGui::Separator();

		// Sequence list table
		float tableHeight = ImGui::GetContentRegionAvail().y - 125.0f;
		if (tableHeight < 120.0f) tableHeight = 120.0f;

		if (ImGui::BeginTable("AnimSeqList", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, tableHeight)))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 125.0f);
			ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn("FPS", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Export", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			int matchCount = 0;
			for (int i = 0; i < totalAnims; i++)
			{
				const CAnimSequence* seq = animSet->Sequences[i];
				if (!seq) continue;

				const char* name = *seq->Name;
				bool bAdditive = seq->bAdditive;
				int addType = seq->AdditiveType;

				// Apply filter
				if (animFilterType == 1 && bAdditive) continue;       // Standard only
				if (animFilterType == 2 && !bAdditive) continue;      // Additive only

				if (searchAnimFilter[0] != '\0')
				{
					if (!appStristr(name, searchAnimFilter))
						continue;
				}

				matchCount++;
				bool bSelected = (skelViewer->AnimIndex == i);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%d", i + 1);

				ImGui::TableNextColumn();
				char label[256];
				appSprintf(ARRAY_ARG(label), "%s##seq%d", name, i);
				if (ImGui::Selectable(label, bSelected, ImGuiSelectableFlags_SpanAllColumns))
				{
					skelViewer->SelectAnim(i);
					if (bAnimPlaying)
						skelViewer->PlayCurrentAnim(bAnimLoop, animSpeed);
				}

				// Right-click context popup on sequence row
				if (ImGui::BeginPopupContextItem())
				{
					char exportCtxLabel[128];
					appSprintf(ARRAY_ARG(exportCtxLabel), "Export '%s'", name);
					if (ImGui::MenuItem(exportCtxLabel))
					{
						skelViewer->ExportAnimation(i);
						appSprintf(ARRAY_ARG(exportStatusMsg), "Exported: %s", name);
						exportStatusTimer = SDL_GetTicks() + 4000;
					}
					if (ImGui::MenuItem("Play Animation"))
					{
						skelViewer->SelectAnim(i);
						if (bAnimPlaying)
							skelViewer->PlayCurrentAnim(bAnimLoop, animSpeed);
					}
					if (ImGui::MenuItem("Copy Name"))
					{
						ImGui::SetClipboardText(name);
					}
					ImGui::EndPopup();
				}

				ImGui::TableNextColumn();
				if (bAdditive)
				{
					if (addType == 2)
						ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[Additive: Mesh]");
					else
						ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "[Additive: Local]");
				}
				else
				{
					ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.7f, 1.0f), "[Standard]");
				}

				ImGui::TableNextColumn();
				ImGui::Text("%d", seq ? seq->NumFrames : 0);

				ImGui::TableNextColumn();
				ImGui::Text("%.1f", seq ? seq->Rate : 0.0f);

				ImGui::TableNextColumn();
				float duration = (seq && seq->Rate > 0) ? (float)seq->NumFrames / seq->Rate : 0.0f;
				ImGui::Text("%.2fs", duration);

				ImGui::TableNextColumn();
				char exportBtnId[64];
				appSprintf(ARRAY_ARG(exportBtnId), "Export##btn%d", i);
				if (ImGui::SmallButton(exportBtnId))
				{
					skelViewer->ExportAnimation(i);
					appSprintf(ARRAY_ARG(exportStatusMsg), "Exported: %s", name);
					exportStatusTimer = SDL_GetTicks() + 4000;
				}
			}
			ImGui::EndTable();
		}

		ImGui::Separator();

		const CAnimSequence* currentSeq = meshInst->GetAnim(0);
		float currentFrame = meshInst->GetAnimFrame(0);
		float maxFrames = (currentSeq && currentSeq->NumFrames > 1) ? (float)(currentSeq->NumFrames - 1) : 0.0f;

		// Synchronize bAnimPlaying with playback state
		if (!bAnimLoop && currentSeq && currentSeq->NumFrames > 1 && currentFrame >= maxFrames)
		{
			bAnimPlaying = false;
		}
		else if (meshInst->GetAnimRate(0) != 0.0f)
		{
			bAnimPlaying = true;
		}

		// Transport Controls
		if (ImGui::Button(" |< "))
		{
			skelViewer->SetAnimFrame(0.0f);
			bAnimPlaying = false;
		}
		ImGui::SameLine();
		if (ImGui::Button(" < "))
		{
			float newF = bound(currentFrame - 0.2f, 0.0f, maxFrames);
			skelViewer->SetAnimFrame(newF);
			bAnimPlaying = false;
		}
		ImGui::SameLine();
		if (ImGui::Button(bAnimPlaying ? " Pause " : " Play "))
		{
			TogglePlayPause();
		}
		ImGui::SameLine();
		if (ImGui::Button(" > "))
		{
			float newF = bound(currentFrame + 0.2f, 0.0f, maxFrames);
			skelViewer->SetAnimFrame(newF);
			bAnimPlaying = false;
		}
		ImGui::SameLine();
		if (ImGui::Button(" >| "))
		{
			skelViewer->SetAnimFrame(maxFrames);
			bAnimPlaying = false;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Loop", &bAnimLoop))
		{
			if (bAnimPlaying)
				skelViewer->PlayCurrentAnim(bAnimLoop, animSpeed);
		}

		// Timeline Scrubber
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200.0f);
		float scrubVal = currentFrame;
		char frameFmt[64];
		appSprintf(ARRAY_ARG(frameFmt), "Frame: %4.1f / %.0f", currentFrame, maxFrames + 1.0f);
		if (ImGui::SliderFloat("##Timeline", &scrubVal, 0.0f, maxFrames, frameFmt))
		{
			skelViewer->SetAnimFrame(scrubVal);
			bAnimPlaying = false;
		}

		// Playback speed
		ImGui::SameLine();
		ImGui::SetNextItemWidth(75.0f);
		if (ImGui::SliderFloat("Speed", &animSpeed, 0.1f, 3.0f, "%.2fx"))
		{
			skelViewer->SetAnimSpeed(animSpeed);
		}
		if (ImGui::BeginPopupContextItem("SpeedContext"))
		{
			if (ImGui::MenuItem("Reset to 1.0x"))
			{
				animSpeed = 1.0f;
				skelViewer->SetAnimSpeed(1.0f);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("0.25x (Slow)"))
			{
				animSpeed = 0.25f;
				skelViewer->SetAnimSpeed(0.25f);
			}
			if (ImGui::MenuItem("0.50x"))
			{
				animSpeed = 0.5f;
				skelViewer->SetAnimSpeed(0.5f);
			}
			if (ImGui::MenuItem("1.00x (Normal)"))
			{
				animSpeed = 1.0f;
				skelViewer->SetAnimSpeed(1.0f);
			}
			if (ImGui::MenuItem("1.50x"))
			{
				animSpeed = 1.5f;
				skelViewer->SetAnimSpeed(1.5f);
			}
			if (ImGui::MenuItem("2.00x (Fast)"))
			{
				animSpeed = 2.0f;
				skelViewer->SetAnimSpeed(2.0f);
			}
			ImGui::EndPopup();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Playback speed multiplier\nRight-click for presets / Reset to 1.0x");
		}

		// Reset Speed Button
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset##Speed"))
		{
			animSpeed = 1.0f;
			skelViewer->SetAnimSpeed(1.0f);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Reset speed to 1.0x");
		}

		// Row 2: Export Selected Animation Button
		ImGui::Spacing();
		const char* curSelectedName = (skelViewer->AnimIndex >= 0 && skelViewer->AnimIndex < totalAnims)
			? *animSet->Sequences[skelViewer->AnimIndex]->Name : "None";
		char exportBtnLabel[128];
		appSprintf(ARRAY_ARG(exportBtnLabel), " Export Selected Animation (%s) ", curSelectedName);
		if (ImGui::Button(exportBtnLabel))
		{
			if (skelViewer->AnimIndex >= 0 && skelViewer->AnimIndex < totalAnims)
			{
				skelViewer->ExportAnimation(skelViewer->AnimIndex);
				appSprintf(ARRAY_ARG(exportStatusMsg), "Exported: %s", curSelectedName);
				exportStatusTimer = SDL_GetTicks() + 4000;
			}
		}
	}
	ImGui::End();
}

static void DrawOverlayToggle()
{
	if (bShowUI) return;

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

	ImGui::SetNextWindowPos(ImVec2(16, 16));
	ImGui::SetNextWindowBgAlpha(0.70f);

	if (ImGui::Begin("##OverlayToggle", NULL, flags))
	{
		if (ImGui::Button(" Open UI (F1) "))
		{
			ToggleVisible();
		}
	}
	ImGui::End();
}

void Draw(CObjectViewer* viewer)
{
	if (!bInitialized) return;

	GCurrentViewer = viewer;

	if (bShowUI)
	{
		DrawMainMenuBar(viewer);
		DrawInspectorPanel(viewer);

		CSkelMeshViewer* skelViewer = viewer ? viewer->AsSkelMeshViewer() : NULL;
		if (skelViewer)
		{
			DrawAnimationStudio(skelViewer);
		}
	}
	else
	{
		DrawOverlayToggle();
	}
}

void Render()
{
	if (!bInitialized) return;

	ImGui::Render();

	// Ensure no custom shader is active for OpenGL2 fixed function pipeline
	if (GUseGLSL)
	{
		glUseProgram(0);
	}

	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

} // namespace ViewerUI

#endif // RENDERING
