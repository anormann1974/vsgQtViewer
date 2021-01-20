#include "SkyBox.h"

#include <cstdint>
#include "data/skybox_frag.h"
#include "data/skybox_vert.h"

#include <vsg/state/StateGroup.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/state/DescriptorImage.h>

namespace {
vsg::ref_ptr<vsg::Data> createTexture(const vsg::vec4 &color)
{
    auto vsg_data = vsg::vec4Array2D::create(1, 1, vsg::Data::Layout{VK_FORMAT_R32G32B32A32_SFLOAT});
    std::fill(vsg_data->begin(), vsg_data->end(), color);
    return vsg_data;
}

static vsg::vec4 kWhiteColor{1.0, 1.0, 1.0, 1.0};
static auto kWhiteData = createTexture(kWhiteColor);
}

vsg::ref_ptr<vsg::Node> createSkybox()
{
    auto vertexShader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vsg::ShaderModule::SPIRV(skybox_vert, skybox_vert + sizeof(skybox_vert) / sizeof(*skybox_vert)));
    auto fragmentShader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::SPIRV(skybox_frag, skybox_frag + sizeof(skybox_frag) / sizeof(*skybox_frag)));
    const vsg::ShaderStages shaders{vertexShader, fragmentShader};


    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}
    };

    auto rasterState = vsg::RasterizationState::create();
    rasterState->cullMode = VK_CULL_MODE_FRONT_BIT;

    auto depthState = vsg::DepthStencilState::create();
    depthState->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthState->minDepthBounds = 1.0f;
    depthState->maxDepthBounds = 1.0f;

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        vsg::InputAssemblyState::create(),
        rasterState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        depthState
    };

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(pipeline);

    // create texture image and associated DescriptorSets and binding
    auto sampler = vsg::Sampler::create();
    auto texture = vsg::DescriptorImage::create(sampler, kWhiteData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{texture});
    auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);

    auto root = vsg::StateGroup::create();
    root->add(bindGraphicsPipeline);
    root->add(bindDescriptorSet);

    vsg::ref_ptr<vsg::vec3Array> vertices(new vsg::vec3Array({// Back
                                                              {-1.0f, -1.0f, -1.0f},
                                                              {1.0f, -1.0f, -1.0f},
                                                              {-1.0f, 1.0f, -1.0f},
                                                              {1.0f, 1.0f, -1.0f},

                                                              // Front
                                                              {-1.0f, -1.0f, 1.0f},
                                                              {1.0f, -1.0f, 1.0f},
                                                              {-1.0f, 1.0f, 1.0f},
                                                              {1.0f, 1.0f, 1.0f},

                                                              // Left
                                                              {-1.0f, -1.0f, -1.0f},
                                                              {-1.0f, -1.0f, 1.0f},
                                                              {-1.0f, 1.0f, -1.0f},
                                                              {-1.0f, 1.0f, 1.0f},

                                                              // Right
                                                              {1.0f, -1.0f, -1.0f},
                                                              {1.0f, -1.0f, 1.0f},
                                                              {1.0f, 1.0f, -1.0f},
                                                              {1.0f, 1.0f, 1.0f},

                                                              // Bottom
                                                              {-1.0f, -1.0f, -1.0f},
                                                              {-1.0f, -1.0f, 1.0f},
                                                              {1.0f, -1.0f, -1.0f},
                                                              {1.0f, -1.0f, 1.0f},

                                                              // Top
                                                              {-1.0f, 1.0f, -1.0f},
                                                              {-1.0f, 1.0f, 1.0f},
                                                              {1.0f, 1.0f, -1.0f},
                                                              {1.0f, 1.0f, 1.0} }));

    vsg::ref_ptr<vsg::ushortArray> indices(new vsg::ushortArray({// Back
                                                                 0, 2, 1,
                                                                 1, 2, 3,

                                                                 // Front
                                                                 6, 4, 5,
                                                                 7, 6, 5,

                                                                 // Left
                                                                 10, 8, 9,
                                                                 11, 10, 9,

                                                                 // Right
                                                                 14, 13, 12,
                                                                 15, 13, 14,

                                                                 // Bottom
                                                                 17, 16, 19,
                                                                 19, 16, 18,

                                                                 // Top
                                                                 23, 20, 21,
                                                                 22, 20, 23 }));

    root->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
    root->addChild(vsg::BindIndexBuffer::create(indices));
    //root->addChild(vsg::DrawIndexed::create(indices->size(), 1, 0, 0, 0));

    return root;
}
