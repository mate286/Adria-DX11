# Adria-DX11

Graphics engine written in C++/DirectX11.

## Features
* Entity-Component System
* Deferred + Forward Rendering 
* Tiled Deferred Rendering 
* Clustered Deferred Rendering
* Voxel Cone Tracing Global Illumination
* Physically Based Shading
* Image Based Lighting
* Normal Mapping
* Shadows
    - PCF Shadows for Directional, Spot and Point lights
    - Cascade Shadow Maps for Directional Lights
* Volumetric Lighting
    - Directional Lights with Shadow Maps
    - Directional Lights with Cascade Shadow Maps
    - Point and Spot Lights 
* HDR and Tone Mapping
* Bloom
* Depth Of Field + Bokeh  
    - Bokeh shapes supported - Hexagon, Octagon, Circle, Cross
* Ambient Occlusion: SSAO, HBAO
* SSR
* FXAA
* TAA
* God Rays
* Lens Flare
* Fog
* Motion Blur
* Volumetric Clouds
* Ocean FFT
    - Adaptive Tesselation
    - Foam
* Procedural Terrain with Instanced Foliage and Trees
* Hosek-Wilkie Sky Model
* ImGui Editor
* Model Loading with tinygltf
* Profiler
* Camera and Light Frustum Culling


## Dependencies
[tinygltf](https://github.com/syoyo/tinygltf)

[ImGui](https://github.com/ocornut/imgui)

[ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)

[ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog)

[stb](https://github.com/nothings/stb)

[FastNoiseLite](https://github.com/Auburn/FastNoiseLite)

## Screenshots

Editor 
![alt text](Screenshots/editor.png "Editor")

<table>
  <tr>
    <td>Tiled Deferred Rendering with 256 lights</td>
     <td>Tiled Deferred Rendering Visualized</td>
     </tr>
  <tr>
    <td><img src="Screenshots/tiled.png"></td>
    <td><img src="Screenshots/tiled_visualization.png"></td>
  </tr>
 </table>
 
 <table>
  <tr>
     <td>Voxel Cone Tracing Global Illumination</td>
     <td>Voxelized Scene</td>
     </tr>
  <tr>
    <td><img src="Screenshots/gi.png"></td>
    <td><img src="Screenshots/gi_debug.png"></td>
  </tr>
 </table>
 
 Image Based Lighting 
![alt text](Screenshots/ibl.png "Image Based Lighting ")
 
Ocean and Lens Flare
![alt text](Screenshots/ocean_lens_flare.png "Ocean and Lens Flare")
 
 Volumetric Lighting
![alt text](Screenshots/volumetric_dir.png " Volumetric Directional Lighting")
![alt text](Screenshots/volumetric_point.png " Volumetric Point Lighting")

Bokeh
![alt text](Screenshots/bokeh.png "Bokeh")

Volumetric Clouds
![alt text](Screenshots/clouds.png "Clouds")

Hosek-Wilkie Sky Model
![alt text](Screenshots/hosek_wilkie.png "Hosek-Wilkie")

God Rays and Instanced Foliage
![alt text](Screenshots/foliage2.gif "Instanced Foliage")


