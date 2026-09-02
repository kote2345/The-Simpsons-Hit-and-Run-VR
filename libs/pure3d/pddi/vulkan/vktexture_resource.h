#ifndef PDDI_VULKAN_TEXTURE_RESOURCE_H
#define PDDI_VULKAN_TEXTURE_RESOURCE_H

#include <vector>
class pddiTexture;

// Copies the authored level-zero BGRA pixels retained by the Vulkan PDDI
// texture. This is a resource bridge, not a render/capture path.
bool pddiVulkanCopyTexturePixels(pddiTexture* texture,
                                 std::vector<unsigned char>* pixels,
                                 unsigned* width,unsigned* height);

#endif
