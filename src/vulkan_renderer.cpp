#include "vulkan_renderer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct PushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t sphere_count;
    uint32_t samples_per_pixel;
    uint32_t max_depth;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
    float camera_center[4];
    float pixel00_loc[4];
    float pixel_delta_u[4];
    float pixel_delta_v[4];
    float defocus_disk_u[4];
    float defocus_disk_v[4];
};

struct GpuSphere {
    float center_x;
    float center_y;
    float center_z;
    float radius;
    float albedo_r;
    float albedo_g;
    float albedo_b;
    float material_type;
    float fuzz;
    float refraction_index;
    float padding0;
    float padding1;
};

struct Vec3 {
    double x;
    double y;
    double z;
};

Vec3 make_vec3(double x, double y, double z) {
    Vec3 v = {x, y, z};
    return v;
}

Vec3 add(Vec3 a, Vec3 b) {
    return make_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 subtract(Vec3 a, Vec3 b) {
    return make_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 multiply(Vec3 v, double t) {
    return make_vec3(v.x * t, v.y * t, v.z * t);
}

Vec3 divide(Vec3 v, double t) {
    return make_vec3(v.x / t, v.y / t, v.z / t);
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return make_vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

double length(Vec3 v) {
    return std::sqrt(dot(v, v));
}

Vec3 unit_vector(Vec3 v) {
    return divide(v, length(v));
}

double degrees_to_radians(double degrees) {
    return degrees * 3.1415926535897932385 / 180.0;
}

double demo_random_double(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state + 0.5) / 4294967296.0;
}

double demo_random_double(uint32_t& state, double min_value, double max_value) {
    return min_value + (max_value - min_value) * demo_random_double(state);
}

GpuSphere make_sphere(
    double center_x,
    double center_y,
    double center_z,
    double radius,
    double albedo_r,
    double albedo_g,
    double albedo_b,
    double material_type,
    double fuzz,
    double refraction_index
) {
    GpuSphere sphere = {
        static_cast<float>(center_x),
        static_cast<float>(center_y),
        static_cast<float>(center_z),
        static_cast<float>(radius),
        static_cast<float>(albedo_r),
        static_cast<float>(albedo_g),
        static_cast<float>(albedo_b),
        static_cast<float>(material_type),
        static_cast<float>(fuzz),
        static_cast<float>(refraction_index),
        0.0f,
        0.0f
    };
    return sphere;
}

std::vector<GpuSphere> create_demo_scene() {
    std::vector<GpuSphere> spheres;
    spheres.push_back(make_sphere(0.0, -1000.0, 0.0, 1000.0, 0.5, 0.5, 0.5, 0.0, 0.0, 1.0));

    uint32_t rng_state = 0x12345678u;
    for (int a = -11; a < 11; ++a) {
        for (int b = -11; b < 11; ++b) {
            const double center_x = a + 0.9 * demo_random_double(rng_state);
            const double center_z = b + 0.9 * demo_random_double(rng_state);
            const Vec3 center = make_vec3(center_x, 0.2, center_z);

            if (dot(subtract(center, make_vec3(4.0, 0.2, 0.0)), subtract(center, make_vec3(4.0, 0.2, 0.0))) <= 0.81) {
                continue;
            }

            const double choose_material = demo_random_double(rng_state);
            if (choose_material < 0.8) {
                const double r = demo_random_double(rng_state) * demo_random_double(rng_state);
                const double g = demo_random_double(rng_state) * demo_random_double(rng_state);
                const double bl = demo_random_double(rng_state) * demo_random_double(rng_state);
                spheres.push_back(make_sphere(center.x, center.y, center.z, 0.2, r, g, bl, 0.0, 0.0, 1.0));
            } else if (choose_material < 0.95) {
                const double r = demo_random_double(rng_state, 0.5, 1.0);
                const double g = demo_random_double(rng_state, 0.5, 1.0);
                const double bl = demo_random_double(rng_state, 0.5, 1.0);
                const double fuzz = demo_random_double(rng_state, 0.0, 0.5);
                spheres.push_back(make_sphere(center.x, center.y, center.z, 0.2, r, g, bl, 1.0, fuzz, 1.0));
            } else {
                spheres.push_back(make_sphere(center.x, center.y, center.z, 0.2, 1.0, 1.0, 1.0, 2.0, 0.0, 1.5));
            }
        }
    }

    spheres.push_back(make_sphere(0.0, 1.0, 0.0, 1.0, 1.0, 1.0, 1.0, 2.0, 0.0, 1.5));
    spheres.push_back(make_sphere(-4.0, 1.0, 0.0, 1.0, 0.4, 0.2, 0.1, 0.0, 0.0, 1.0));
    spheres.push_back(make_sphere(4.0, 1.0, 0.0, 1.0, 0.7, 0.6, 0.5, 1.0, 0.0, 1.0));

    return spheres;
}

void check(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

std::vector<char> read_binary_file(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("failed to open shader file: ") + path);
    }

    const std::streamsize size = file.tellg();
    std::vector<char> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

uint32_t find_memory_type(
    VkPhysicalDevice physical_device,
    uint32_t type_filter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool matches_type = (type_filter & (1u << i)) != 0;
        const bool has_properties =
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties;
        if (matches_type && has_properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable Vulkan memory type");
}

class VulkanComputeRenderer {
public:
    VulkanComputeRenderer(
        int width,
        int height,
        const char* shader_spv_path,
        const std::vector<GpuSphere>& spheres = std::vector<GpuSphere>()
    )
        : m_width(width),
          m_height(height),
          m_shader_spv_path(shader_spv_path),
          m_spheres(spheres) {}

    ~VulkanComputeRenderer() {
        cleanup();
    }

    void set_sampling(int samples_per_pixel, int max_depth) {
        m_samples_per_pixel = samples_per_pixel;
        m_max_depth = max_depth;
    }

    void set_camera(
        Vec3 lookfrom,
        Vec3 lookat,
        Vec3 vup,
        double vfov,
        double defocus_angle,
        double focus_dist
    ) {
        const double image_height = m_height < 1 ? 1.0 : static_cast<double>(m_height);
        const double theta = degrees_to_radians(vfov);
        const double h = std::tan(theta / 2.0);
        const double viewport_height = 2.0 * h * focus_dist;
        const double viewport_width = viewport_height * (static_cast<double>(m_width) / image_height);

        const Vec3 w = unit_vector(subtract(lookfrom, lookat));
        const Vec3 u = unit_vector(cross(vup, w));
        const Vec3 v = cross(w, u);

        const Vec3 viewport_u = multiply(u, viewport_width);
        const Vec3 viewport_v = multiply(v, -viewport_height);
        const Vec3 pixel_delta_u = divide(viewport_u, static_cast<double>(m_width));
        const Vec3 pixel_delta_v = divide(viewport_v, image_height);
        const Vec3 viewport_upper_left = subtract(
            subtract(subtract(lookfrom, multiply(w, focus_dist)), divide(viewport_u, 2.0)),
            divide(viewport_v, 2.0)
        );
        const Vec3 pixel00_loc = add(viewport_upper_left, multiply(add(pixel_delta_u, pixel_delta_v), 0.5));

        const double defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2.0));
        const Vec3 defocus_disk_u = multiply(u, defocus_radius);
        const Vec3 defocus_disk_v = multiply(v, defocus_radius);

        set_vec4(m_camera_center, lookfrom);
        set_vec4(m_pixel00_loc, pixel00_loc);
        set_vec4(m_pixel_delta_u, pixel_delta_u);
        set_vec4(m_pixel_delta_v, pixel_delta_v);
        set_vec4(m_defocus_disk_u, defocus_disk_u);
        set_vec4(m_defocus_disk_v, defocus_disk_v);
    }

    void render(const char* output_file) {
        create_instance();
        pick_physical_device();
        create_logical_device();
        create_output_buffer();
        create_sphere_buffer();
        create_descriptor_set();
        create_compute_pipeline();
        create_command_buffer();
        run_compute_shader();
        write_ppm(output_file);
    }

private:
    int m_width = 0;
    int m_height = 0;
    int m_samples_per_pixel = 1;
    int m_max_depth = 1;
    float m_camera_center[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_pixel00_loc[4] = {-1.0f, 1.0f, -1.0f, 0.0f};
    float m_pixel_delta_u[4] = {0.01f, 0.0f, 0.0f, 0.0f};
    float m_pixel_delta_v[4] = {0.0f, -0.01f, 0.0f, 0.0f};
    float m_defocus_disk_u[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_defocus_disk_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const char* m_shader_spv_path = nullptr;
    std::vector<GpuSphere> m_spheres;
    uint32_t m_queue_family_index = 0;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;

    VkBuffer m_output_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_output_memory = VK_NULL_HANDLE;
    VkBuffer m_sphere_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sphere_memory = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;

    VkShaderModule m_shader_module = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkCommandPool m_command_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_command_buffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

    void set_vec4(float destination[4], Vec3 value) {
        destination[0] = static_cast<float>(value.x);
        destination[1] = static_cast<float>(value.y);
        destination[2] = static_cast<float>(value.z);
        destination[3] = 0.0f;
    }

    void create_instance() {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "RayTracingInOneWeekendVulkan";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "NoEngine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;

        check(vkCreateInstance(&create_info, nullptr, &m_instance), "failed to create Vulkan instance");
    }

    void pick_physical_device() {
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr), "failed to enumerate Vulkan physical devices");
        if (device_count == 0) {
            throw std::runtime_error("no Vulkan physical devices found");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data()), "failed to get Vulkan physical devices");

        for (size_t device_index = 0; device_index < devices.size(); ++device_index) {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, nullptr);

            std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, queue_families.data());

            for (uint32_t i = 0; i < queue_family_count; ++i) {
                if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    m_physical_device = devices[device_index];
                    m_queue_family_index = i;
                    return;
                }
            }
        }

        throw std::runtime_error("failed to find a Vulkan device with a compute queue");
    }

    void create_logical_device() {
        const float queue_priority = 1.0f;

        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = m_queue_family_index;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_create_info;

        check(vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device), "failed to create Vulkan logical device");
        vkGetDeviceQueue(m_device, m_queue_family_index, 0, &m_queue);
    }

    void create_output_buffer() {
        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(m_width) *
            static_cast<VkDeviceSize>(m_height) * sizeof(uint32_t);

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        check(vkCreateBuffer(m_device, &buffer_info, nullptr, &m_output_buffer), "failed to create Vulkan output buffer");

        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(m_device, m_output_buffer, &memory_requirements);

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.allocationSize = memory_requirements.size;
        allocate_info.memoryTypeIndex = find_memory_type(
            m_physical_device,
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        check(vkAllocateMemory(m_device, &allocate_info, nullptr, &m_output_memory), "failed to allocate Vulkan output memory");
        check(vkBindBufferMemory(m_device, m_output_buffer, m_output_memory, 0), "failed to bind Vulkan output buffer memory");
    }

    void create_sphere_buffer() {
        const size_t sphere_count = m_spheres.empty() ? 1 : m_spheres.size();
        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(sphere_count) * sizeof(GpuSphere);

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        check(vkCreateBuffer(m_device, &buffer_info, nullptr, &m_sphere_buffer), "failed to create Vulkan sphere buffer");

        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(m_device, m_sphere_buffer, &memory_requirements);

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.allocationSize = memory_requirements.size;
        allocate_info.memoryTypeIndex = find_memory_type(
            m_physical_device,
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        check(vkAllocateMemory(m_device, &allocate_info, nullptr, &m_sphere_memory), "failed to allocate Vulkan sphere memory");
        check(vkBindBufferMemory(m_device, m_sphere_buffer, m_sphere_memory, 0), "failed to bind Vulkan sphere buffer memory");

        if (!m_spheres.empty()) {
            void* mapped = nullptr;
            check(vkMapMemory(m_device, m_sphere_memory, 0, buffer_size, 0, &mapped), "failed to map Vulkan sphere memory");
            std::memcpy(mapped, m_spheres.data(), m_spheres.size() * sizeof(GpuSphere));
            vkUnmapMemory(m_device, m_sphere_memory);
        }
    }

    void create_descriptor_set() {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 2;
        layout_info.pBindings = bindings;

        check(vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &m_descriptor_set_layout), "failed to create Vulkan descriptor set layout");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 2;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;

        check(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptor_pool), "failed to create Vulkan descriptor pool");

        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = m_descriptor_pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &m_descriptor_set_layout;

        check(vkAllocateDescriptorSets(m_device, &allocate_info, &m_descriptor_set), "failed to allocate Vulkan descriptor set");

        VkDescriptorBufferInfo output_buffer_info{};
        output_buffer_info.buffer = m_output_buffer;
        output_buffer_info.offset = 0;
        output_buffer_info.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo sphere_buffer_info{};
        sphere_buffer_info.buffer = m_sphere_buffer;
        sphere_buffer_info.offset = 0;
        sphere_buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet descriptor_writes[2]{};
        descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[0].dstSet = m_descriptor_set;
        descriptor_writes[0].dstBinding = 0;
        descriptor_writes[0].descriptorCount = 1;
        descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptor_writes[0].pBufferInfo = &output_buffer_info;

        descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[1].dstSet = m_descriptor_set;
        descriptor_writes[1].dstBinding = 1;
        descriptor_writes[1].descriptorCount = 1;
        descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptor_writes[1].pBufferInfo = &sphere_buffer_info;

        vkUpdateDescriptorSets(m_device, 2, descriptor_writes, 0, nullptr);
    }

    void create_compute_pipeline() {
        const std::vector<char> shader_code = read_binary_file(m_shader_spv_path);

        VkShaderModuleCreateInfo shader_info{};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = shader_code.size();
        shader_info.pCode = reinterpret_cast<const uint32_t*>(shader_code.data());

        check(vkCreateShaderModule(m_device, &shader_info, nullptr, &m_shader_module), "failed to create Vulkan shader module");

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &m_descriptor_set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant_range;

        check(vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_pipeline_layout), "failed to create Vulkan pipeline layout");

        VkPipelineShaderStageCreateInfo shader_stage_info{};
        shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shader_stage_info.module = m_shader_module;
        shader_stage_info.pName = "main";

        VkComputePipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = shader_stage_info;
        pipeline_info.layout = m_pipeline_layout;

        check(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline), "failed to create Vulkan compute pipeline");
    }

    void create_command_buffer() {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = m_queue_family_index;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        check(vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool), "failed to create Vulkan command pool");

        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = m_command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;

        check(vkAllocateCommandBuffers(m_device, &allocate_info, &m_command_buffer), "failed to allocate Vulkan command buffer");

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(m_device, &fence_info, nullptr, &m_fence), "failed to create Vulkan fence");
    }

    void run_compute_shader() {
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        check(vkBeginCommandBuffer(m_command_buffer, &begin_info), "failed to begin Vulkan command buffer");

        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(
            m_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            m_pipeline_layout,
            0,
            1,
            &m_descriptor_set,
            0,
            nullptr
        );

        const PushConstants push_constants{
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            static_cast<uint32_t>(m_spheres.size()),
            static_cast<uint32_t>(m_samples_per_pixel),
            static_cast<uint32_t>(m_max_depth),
            0,
            0,
            0,
            {
                m_camera_center[0],
                m_camera_center[1],
                m_camera_center[2],
                m_camera_center[3]
            },
            {
                m_pixel00_loc[0],
                m_pixel00_loc[1],
                m_pixel00_loc[2],
                m_pixel00_loc[3]
            },
            {
                m_pixel_delta_u[0],
                m_pixel_delta_u[1],
                m_pixel_delta_u[2],
                m_pixel_delta_u[3]
            },
            {
                m_pixel_delta_v[0],
                m_pixel_delta_v[1],
                m_pixel_delta_v[2],
                m_pixel_delta_v[3]
            },
            {
                m_defocus_disk_u[0],
                m_defocus_disk_u[1],
                m_defocus_disk_u[2],
                m_defocus_disk_u[3]
            },
            {
                m_defocus_disk_v[0],
                m_defocus_disk_v[1],
                m_defocus_disk_v[2],
                m_defocus_disk_v[3]
            }
        };
        vkCmdPushConstants(
            m_command_buffer,
            m_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(PushConstants),
            &push_constants
        );

        const uint32_t group_count_x = (static_cast<uint32_t>(m_width) + 15u) / 16u;
        const uint32_t group_count_y = (static_cast<uint32_t>(m_height) + 15u) / 16u;
        vkCmdDispatch(m_command_buffer, group_count_x, group_count_y, 1);

        check(vkEndCommandBuffer(m_command_buffer), "failed to end Vulkan command buffer");

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &m_command_buffer;

        check(vkQueueSubmit(m_queue, 1, &submit_info, m_fence), "failed to submit Vulkan compute work");
        check(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX), "failed while waiting for Vulkan compute work");
    }

    void write_ppm(const char* output_file) {
        const size_t pixel_count = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
        std::vector<uint32_t> pixels(pixel_count);

        void* mapped = nullptr;
        check(vkMapMemory(m_device, m_output_memory, 0, VK_WHOLE_SIZE, 0, &mapped), "failed to map Vulkan output memory");
        std::memcpy(pixels.data(), mapped, pixel_count * sizeof(uint32_t));
        vkUnmapMemory(m_device, m_output_memory);

        std::ofstream image_file(output_file);
        if (!image_file) {
            throw std::runtime_error(std::string("failed to open output file: ") + output_file);
        }

        image_file << "P3\n" << m_width << ' ' << m_height << "\n255\n";
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                const uint32_t pixel = pixels[static_cast<size_t>(y) * m_width + x];
                const uint32_t r = (pixel >> 16) & 0xffu;
                const uint32_t g = (pixel >> 8) & 0xffu;
                const uint32_t b = pixel & 0xffu;
                image_file << r << ' ' << g << ' ' << b << '\n';
            }
        }
    }

    void cleanup() {
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
        }

        if (m_fence != VK_NULL_HANDLE) vkDestroyFence(m_device, m_fence, nullptr);
        if (m_command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
        if (m_shader_module != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_shader_module, nullptr);
        if (m_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
        if (m_descriptor_set_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);
        if (m_output_buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_output_buffer, nullptr);
        if (m_output_memory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_output_memory, nullptr);
        if (m_sphere_buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_sphere_buffer, nullptr);
        if (m_sphere_memory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_sphere_memory, nullptr);
        if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
        if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
    }
};

} // namespace

void render_vulkan_gradient(const char* output_file, int image_width, int image_height) {
    VulkanComputeRenderer renderer(image_width, image_height, "shaders/gradient.comp.spv");
    renderer.render(output_file);

    std::clog
        << "Vulkan compute gradient written to " << output_file << '\n'
        << "  image: " << image_width << "x" << image_height << '\n';
}

void render_vulkan_single_sphere(const char* output_file, int image_width, int image_height) {
    VulkanComputeRenderer renderer(image_width, image_height, "shaders/sphere.comp.spv");
    renderer.render(output_file);

    std::clog
        << "Vulkan compute single sphere written to " << output_file << '\n'
        << "  image: " << image_width << "x" << image_height << '\n';
}

void render_vulkan_multiple_spheres(const char* output_file, int image_width, int image_height) {
    const std::vector<GpuSphere> spheres{
        {0.0f, -100.5f, -1.0f, 100.0f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.5f, 0.1f, 0.2f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 2.0f, 0.0f, 1.5f, 0.0f, 0.0f},
        {1.0f, 0.0f, -1.0f, 0.5f, 0.8f, 0.6f, 0.2f, 1.0f, 0.05f, 1.0f, 0.0f, 0.0f}
    };

    VulkanComputeRenderer renderer(image_width, image_height, "shaders/spheres.comp.spv", spheres);
    renderer.render(output_file);

    std::clog
        << "Vulkan compute multiple spheres written to " << output_file << '\n'
        << "  image: " << image_width << "x" << image_height << '\n'
        << "  spheres: " << spheres.size() << '\n';
}

void render_vulkan_diffuse_spheres(const char* output_file, int image_width, int image_height) {
    const std::vector<GpuSphere> spheres{
        {0.0f, -100.5f, -1.0f, 100.0f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.5f, 0.1f, 0.2f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 2.0f, 0.0f, 1.5f, 0.0f, 0.0f},
        {1.0f, 0.0f, -1.0f, 0.5f, 0.8f, 0.6f, 0.2f, 1.0f, 0.05f, 1.0f, 0.0f, 0.0f}
    };

    VulkanComputeRenderer renderer(image_width, image_height, "shaders/diffuse.comp.spv", spheres);
    renderer.set_sampling(10, 5);
    renderer.render(output_file);

    std::clog
        << "Vulkan compute diffuse spheres written to " << output_file << '\n'
        << "  image: " << image_width << "x" << image_height << '\n'
        << "  spheres: " << spheres.size() << '\n'
        << "  samples_per_pixel: 10\n"
        << "  max_depth: 5\n";
}

void render_vulkan_material_spheres(const char* output_file, int image_width, int image_height) {
    const std::vector<GpuSphere> spheres = create_demo_scene();

    VulkanComputeRenderer renderer(image_width, image_height, "shaders/materials.comp.spv", spheres);
    renderer.set_sampling(10, 10);
    renderer.set_camera(
        make_vec3(13.0, 2.0, 3.0),
        make_vec3(0.0, 0.0, 0.0),
        make_vec3(0.0, 1.0, 0.0),
        20.0,
        0.6,
        10.0
    );
    renderer.render(output_file);

    std::clog
        << "Vulkan compute material spheres written to " << output_file << '\n'
        << "  image: " << image_width << "x" << image_height << '\n'
        << "  spheres: " << spheres.size() << '\n'
        << "  samples_per_pixel: 10\n"
        << "  max_depth: 10\n";
}
