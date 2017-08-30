# Physically Based Rendering
(c) 2017 Michał Siejak

An implementation of physically based shading model & image based lighting in various graphics APIs.

API | Implementation status
--- | ---------------------
OpenGL 4.5 | Done
Vulkan | Pending
Direct3D 12 | Pending

## Bibliography

This implementation of physically based shading is largely based on information obtained from the following courses:

- [Real Shading in Unreal Engine 4](http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf), Brian Karis, SIGGRAPH 2013
- [Moving Frostbite to Physically Based Rendering](https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/), Sébastien Lagarde, Charles de Rousiers, SIGGRAPH 2014

Other resources that helped me in research & implementation:

- [Adopting Physically Based Shading Model](https://seblagarde.wordpress.com/2011/08/17/hello-world/), Sébastien Lagarde
- [Hammersley Points on the Hemisphere](http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html), Holger Dammertz
- [Notes on Importance Sampling](http://blog.tobias-franke.eu/2014/03/30/notes_on_importance_sampling.html), Tobias Franke
- [Specular BRDF Reference](http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html), Brian Karis
- [Microfacet Models for Refraction through Rough Surfaces](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf), Bruce Walter et al., Eurographics Symposium on Rendering, 2007
- [An Inexpensive BRDF Model for Physically-Based Rendering](http://igorsklyar.com/system/documents/papers/28/Schlick94.pdf), Christophe Schlick, Eurographics, 1994
- [GPU-Based Importance Sampling](https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html), Mark Colbert, Jaroslav Křivánek, GPU Gems 3, 2007
- [Physically Based Rendering: From Theory to Implementation, 2nd ed.](https://www.amazon.com/Physically-Based-Rendering-Second-Implementation/dp/0123750792), Matt Pharr, Greg Humphreys, 2010
- [Advanced Global Illumination, 2nd ed.](https://www.amazon.com/Advanced-Global-Illumination-Second-Philip/dp/1568813074), Philip Dutré, Kavita Bala, Philippe Bekaert, 2006
- [Photographic Tone Reproduction for Digital Images](https://www.cs.utah.edu/~reinhard/cdrom/), Erik Reinhard et al., 2002
