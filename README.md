# Physically Based Rendering
(c) 2017 Michał Siejak

An implementation of physically based shading model & image based lighting in various graphics APIs.

![Screenshot](https://media.githubusercontent.com/media/Nadrin/PBR/master/data/screenshot.jpg)

API         | SLOC | Implementation status
------------|------|----------------------
OpenGL 4.5  | 521  | Done
Vulkan      |      | In progress
Direct3D 11 | 694  | Done
Direct3D 12 | 1240 | Done

## Building

### Windows

#### Prerequisites

- Windows 10 or Windows Server 2016 (x64 versions)
- Visual Studio 2017 (any edition)
- Relatively recent version of Windows 10 SDK

#### How to build

Visual Studio solution is available at ```projects/msvc2017/PBR.sln```. After successful build the resulting executable
and all needed DLLs can be found in ```data``` directory. Note that precompiled third party libraries are only available
for x64 target.

### Linux

Coming soon.

## Running

Make sure to run from within ```data``` directory as all paths are relative to it. API to be used can be specified on the command line
as a single parameter (```-opengl```, ```-d3d11```, or ```-d3d12```). When run with no parameters ```-opengl``` is used.

### Controls

Input        | Action
-------------|-------
LMB drag     | Rotate camera
RMB drag     | Rotate 3D model
Scroll wheel | Zoom in/out
F1-F3        | Toggle analytical lights on/off

## Bibliography

This implementation of physically based shading is largely based on information obtained from the following courses:

- [Real Shading in Unreal Engine 4](http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf), Brian Karis, SIGGRAPH 2013
- [Moving Frostbite to Physically Based Rendering](https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/), Sébastien Lagarde, Charles de Rousiers, SIGGRAPH 2014

Other resources that helped me in research & implementation:

- [Adopting Physically Based Shading Model](https://seblagarde.wordpress.com/2011/08/17/hello-world/), Sébastien Lagarde
- [Microfacet Models for Refraction through Rough Surfaces](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf), Bruce Walter et al., Eurographics, 2007
- [An Inexpensive BRDF Model for Physically-Based Rendering](http://igorsklyar.com/system/documents/papers/28/Schlick94.pdf), Christophe Schlick, Eurographics, 1994
- [GPU-Based Importance Sampling](https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html), Mark Colbert, Jaroslav Křivánek, GPU Gems 3, 2007
- [Hammersley Points on the Hemisphere](http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html), Holger Dammertz
- [Notes on Importance Sampling](http://blog.tobias-franke.eu/2014/03/30/notes_on_importance_sampling.html), Tobias Franke
- [Specular BRDF Reference](http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html), Brian Karis
- [To PI or not to PI in game lighting equation](https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/), Sébastien Lagarde
- [Physically Based Rendering: From Theory to Implementation, 2nd ed.](https://www.amazon.com/Physically-Based-Rendering-Second-Implementation/dp/0123750792), Matt Pharr, Greg Humphreys, 2010
- [Advanced Global Illumination, 2nd ed.](https://www.amazon.com/Advanced-Global-Illumination-Second-Philip/dp/1568813074), Philip Dutré, Kavita Bala, Philippe Bekaert, 2006
- [Photographic Tone Reproduction for Digital Images](https://www.cs.utah.edu/~reinhard/cdrom/), Erik Reinhard et al., 2002

## Third party libraries

This project makes use of the following open source libraries:

- [Open Asset Import Library](http://assimp.sourceforge.net/)
- [stb_image](https://github.com/nothings/stb)
- [GLFW](http://www.glfw.org/)
- [GLM](https://glm.g-truc.net/)
- [D3D12 Helper Library](https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12)
- [Glad](https://github.com/Dav1dde/glad) (used to generate OpenGL function loader)

## Included assets

The following assets are bundled with the project:

- "Cerberus" gun model by [Andrew Maximov](http://artisaverb.info).
- HDR environment map by [Bob Groothuis](http://www.bobgroothuis.com/blog/) obtained from [HDRLabs sIBL archive](http://www.hdrlabs.com/sibl/archive.html) (distributed under [CC-BY-NC-SA 3.0](https://creativecommons.org/licenses/by-nc-sa/3.0/us/)).
