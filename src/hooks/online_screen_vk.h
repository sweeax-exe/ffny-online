/* SPDX-License-Identifier: GPL-3.0-or-later
 * The online screens' texture backend off Windows: RGBA bytes to a sampled
 * image through Vulkan, bound the way imgui_impl_vulkan wants (a descriptor
 * set as ImTextureID). Owned by online_screen.cpp; the device comes from the
 * overlay's ImGui_ImplVulkan_InitInfo (pc_ui's overlay_init_backend). */
#if !defined(_WIN32)
#ifndef ONLINE_SCREEN_VK_H
#define ONLINE_SCREEN_VK_H

#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>

// stb_image, vendored by the toolkit (one .c-wide include, here)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "../../tools/xboxrecomp/src/nv2a/stb/stb_image.h"

struct OsVk {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;      // one-time upload commands
};

static OsVk g_osvk;

static void osvk_set(VkDevice dev, VkPhysicalDevice phy, uint32_t qfam, VkQueue q)
{
    if (g_osvk.device == dev) return;
    if (g_osvk.pool) vkDestroyCommandPool(g_osvk.device, g_osvk.pool, nullptr);
    g_osvk = {};
    g_osvk.device = dev;
    g_osvk.physical = phy;
    g_osvk.queue_family = qfam;
    g_osvk.queue = q;
}

static unsigned osvk_memory_type(VkMemoryRequirements req, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(g_osvk.physical, &props);
    for (unsigned i = 0; i < props.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return ~0u;
}

// A one-shot command buffer on the overlay's queue, waited for before returning.
static bool osvk_submit(void (*record)(VkCommandBuffer, void *), void *user)
{
    if (g_osvk.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pi = {};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = g_osvk.queue_family;
        if (vkCreateCommandPool(g_osvk.device, &pi, nullptr, &g_osvk.pool) != VK_SUCCESS) return false;
    }
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_osvk.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(g_osvk.device, &ai, &cb) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return false;
    record(cb, user);
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) return false;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    if (vkQueueSubmit(g_osvk.queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) return false;
    vkQueueWaitIdle(g_osvk.queue);
    return true;
}

// rgba (w*h*4) -> VkImage (sampled) + view + imgui descriptor set. Returns the set, or VK_NULL_HANDLE.
static VkDescriptorSet osvk_texture(const unsigned char *rgba, int w, int h)
{
    if (!g_osvk.device || !rgba || w <= 0 || h <= 0) return VK_NULL_HANDLE;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    VkImage image;
    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_osvk.device, &ii, nullptr, &image) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_osvk.device, image, &req);
    VkMemoryAllocateInfo mi = {};
    mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mi.allocationSize = req.size;
    mi.memoryTypeIndex = osvk_memory_type(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory mem;
    if (mi.memoryTypeIndex == ~0u || vkAllocateMemory(g_osvk.device, &mi, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    if (vkBindImageMemory(g_osvk.device, image, mem, 0) != VK_SUCCESS) {
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }

    VkBuffer staging;
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkDeviceMemory smem;
    if (vkCreateBuffer(g_osvk.device, &bi, nullptr, &staging) != VK_SUCCESS) {
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements sreq;
    vkGetBufferMemoryRequirements(g_osvk.device, staging, &sreq);
    VkMemoryAllocateInfo smi = {};
    smi.allocationSize = sreq.size;
    smi.memoryTypeIndex = osvk_memory_type(sreq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (smi.memoryTypeIndex == ~0u || vkAllocateMemory(g_osvk.device, &smi, nullptr, &smem) != VK_SUCCESS) {
        vkDestroyBuffer(g_osvk.device, staging, nullptr);
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    vkBindBufferMemory(g_osvk.device, staging, smem, 0);
    void *map = nullptr;
    if (vkMapMemory(g_osvk.device, smem, 0, size, 0, &map) != VK_SUCCESS) {
        vkDestroyBuffer(g_osvk.device, staging, nullptr);
        vkFreeMemory(g_osvk.device, smem, nullptr);
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    memcpy(map, rgba, (size_t)size);
    vkUnmapMemory(g_osvk.device, smem);

    struct Upload { VkImage image; VkBuffer staging; VkDeviceSize size; int w, h; } up = { image, staging, size, w, h };
    bool ok = osvk_submit([](VkCommandBuffer cb, void *u) {
        Upload *up = (Upload *)u;
        VkImageMemoryBarrier to_dst = {};
        to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.srcAccessMask = 0;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.image = up->image;
        to_dst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_dst);
        VkBufferImageCopy copy = {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { (uint32_t)up->w, (uint32_t)up->h, 1 };
        vkCmdCopyBufferToImage(cb, up->staging, up->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier to_shader = {};
        to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.image = up->image;
        to_shader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &to_shader);
    }, &up);
    vkDestroyBuffer(g_osvk.device, staging, nullptr);
    vkFreeMemory(g_osvk.device, smem, nullptr);
    if (!ok) {
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }

    VkImageView view;
    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(g_osvk.device, &vi, nullptr, &view) != VK_SUCCESS) {
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!set) {
        vkDestroyImageView(g_osvk.device, view, nullptr);
        vkFreeMemory(g_osvk.device, mem, nullptr);
        vkDestroyImage(g_osvk.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    // the view, image and memory live as long as the texture: imgui owns the set's lifetime
    return set;
}

#endif
#endif
