#include "ReaderWriterAssimp.h"
#include "ReaderWriterStbi.h"
#include "ReaderWriterKTX.h"

#include "data/assimp_vertex.h"
#include "data/assimp_phong.h"
#include "data/assimp_pbr.h"
#include "data/marmorset_pbr.h"

#include <stack>
#include <filesystem>
#include <strstream>
#include <iostream>

#include <vsg/io/FileSystem.h>
#include <vsg/core/Array3D.h>
#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/state/StateGroup.h>
#include <vsg/commands/Commands.h>
#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/io/read.h>
#include <vsg/maths/transform.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>


namespace {

struct Material {
    vsg::vec4 ambient{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 diffuse{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 specular{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 emissive{1.0f, 1.0f, 1.0f, 1.0f};
    float shininess{0.0f};
};

struct PbrMaterial {
    vsg::vec4 albedo{0.0, 0.0, 0.0, 1.0};
    vsg::vec4 emissive{0.0, 0.0, 0.0, 1.0};
    vsg::vec4 metallicRoughness{0.0, 0.0, 0.0, 0.0};
};

static vsg::vec4 kBlackColor{0.0, 0.0, 0.0, 0.0};
static vsg::vec4 kWhiteColor{1.0, 1.0, 1.0, 1.0};
static vsg::vec4 kNormalColor{127.0 / 255.0, 127.0 / 255.0, 1.0, 1.0};

vsg::ref_ptr<vsg::Data> createTexture(const vsg::vec4 &color)
{
    auto vsg_data = vsg::vec4Array2D::create(1, 1, vsg::Data::Layout{VK_FORMAT_R32G32B32A32_SFLOAT});
    std::fill(vsg_data->begin(), vsg_data->end(), color);
    return vsg_data;
}

static auto kWhiteData = createTexture(kWhiteColor);
static auto kBlackData = createTexture(kBlackColor);
static auto kNormalData = createTexture(kNormalColor);

}

ReaderWriterAssimp::ReaderWriterAssimp()
    : _options{vsg::Options::create()}
{
    _shaders.reserve(5);

    auto readerWriter = vsg::CompositeReaderWriter::create();
    readerWriter->add(ReaderWriterStbi::create());
    readerWriter->add(ReaderWriterKTX::create());
    _options->readerWriter = readerWriter;

    auto readSpvFile = [](const uint32_t *content, size_t size, VkShaderStageFlagBits stage) -> vsg::ref_ptr<vsg::ShaderStage> {
        const auto count = size / sizeof(vsg::ShaderModule::SPIRV::value_type);
        vsg::ShaderModule::SPIRV spirv(content, content+count);

        if (auto shader = vsg::ShaderStage::create(stage, "main", spirv); shader.valid())
            return shader;

        return {};
    };

    // Load phong shaders
    if (auto shader = readSpvFile(assimp_vertex, sizeof(assimp_vertex), VK_SHADER_STAGE_VERTEX_BIT); shader.valid())
        _shaders.push_back(shader);

    if (auto shader = readSpvFile(assimp_phong, sizeof(assimp_phong), VK_SHADER_STAGE_FRAGMENT_BIT); shader.valid())
        _shaders.push_back(shader);

    // Load pbr shaders
    if (auto shader = readSpvFile(assimp_vertex, sizeof(assimp_vertex), VK_SHADER_STAGE_VERTEX_BIT); shader.valid())
        _pbrShaders.push_back(shader);

    if (auto shader = readSpvFile(assimp_pbr, sizeof(assimp_pbr), VK_SHADER_STAGE_FRAGMENT_BIT); shader.valid())
        _pbrShaders.push_back(shader);

    std::cout << __func__ << " " << _shaders.size() << " " << _pbrShaders.size() << std::endl;

    createGraphicsPipelines();
}

void ReaderWriterAssimp::createGraphicsPipelines()
{
    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // normal data
        VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}, // texcoord data
        VkVertexInputBindingDescription{3, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // tangents
        VkVertexInputBindingDescription{4, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // bitangets
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // normal data
        VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT,    0}, // texcoord data
        VkVertexInputAttributeDescription{3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // tangents data
        VkVertexInputAttributeDescription{4, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // bitangents data
    };

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindingsDescriptions, vertexAttributeDescriptions),
        vsg::InputAssemblyState::create(),
        vsg::RasterizationState::create(),
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()
    };

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, _shaders, pipelineStates);
    _bindPhongPipeline = vsg::BindGraphicsPipeline::create(pipeline);
    auto pipeline2 = vsg::GraphicsPipeline::create(pipelineLayout, _pbrShaders, pipelineStates);
    _bindPbrPipeline = vsg::BindGraphicsPipeline::create(pipeline2);

    // create texture image and associated DescriptorSets and binding
    auto sampler = vsg::Sampler::create();
    auto diffuseTexture = vsg::DescriptorImage::create(sampler, kWhiteData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto mrTexture = vsg::DescriptorImage::create(sampler, kBlackData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto normalTexture = vsg::DescriptorImage::create(sampler, kNormalData, 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto aoTexture = vsg::DescriptorImage::create(sampler, kWhiteData, 3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto emissiveTexture = vsg::DescriptorImage::create(sampler, kWhiteData, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto material = vsg::DescriptorBuffer::create(vsg::Value<Material>::create(Material()), 10);

    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{material, diffuseTexture, mrTexture, normalTexture, aoTexture, emissiveTexture});
    _bindDefaultPhongDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::processScene(const aiScene *scene, const vsg::Path &basePath) const
{
    int upAxis = 1, upAxisSign = 1;

    if (scene->mMetaData)
    {
        if (!scene->mMetaData->Get("UpAxis", upAxis))
            upAxis = 1;

        if (!scene->mMetaData->Get("UpAxisSign", upAxisSign))
            upAxisSign = 1;
    }

    // Process materials
    auto pipelineLayout = _bindPhongPipeline->pipeline->layout;
    auto bindDescriptorSets = processMaterials(scene, pipelineLayout.get(), basePath);

    auto root = vsg::MatrixTransform::create();

    if (upAxis == 1)
        root->setMatrix(vsg::rotate(vsg::PIf * 0.5f, (float)upAxisSign, 0.0f, 0.0f));

    auto scenegraph = vsg::StateGroup::create();
    scenegraph->add(_bindPhongPipeline);
    scenegraph->add(_bindDefaultPhongDescriptorSet);

    std::stack<std::pair<aiNode*, vsg::ref_ptr<vsg::Group>>> nodes;
    nodes.push({scene->mRootNode, scenegraph});

    while (!nodes.empty())
    {
        auto [node, parent] = nodes.top();

        aiMatrix4x4 m = node->mTransformation;
        m.Transpose();

        auto xform = vsg::MatrixTransform::create();
        xform->setMatrix(vsg::mat4((float*)&m));
        parent->addChild(xform);

        for (unsigned int i=0; i<node->mNumMeshes; ++i)
        {
            auto mesh = scene->mMeshes[node->mMeshes[i]];
            vsg::ref_ptr<vsg::vec3Array> vertices(new vsg::vec3Array(mesh->mNumVertices));
            vsg::ref_ptr<vsg::vec3Array> normals(new vsg::vec3Array(mesh->mNumVertices));
            vsg::ref_ptr<vsg::vec2Array> texcoords(new vsg::vec2Array(mesh->mNumVertices));
            vsg::ref_ptr<vsg::vec3Array> tangents(new vsg::vec3Array(mesh->mNumVertices));
            vsg::ref_ptr<vsg::vec3Array> bitangents(new vsg::vec3Array(mesh->mNumVertices));
            std::vector<unsigned int> indices;

            for (unsigned int j=0; j<mesh->mNumVertices; ++j)
            {
                vertices->at(j) = vsg::vec3(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);

                if (mesh->mNormals)
                {
                    normals->at(j) = vsg::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
                }
                else
                {
                    normals->at(j) = vsg::vec3(0,0,0);
                }

                if (mesh->mTextureCoords[0])
                {
                    texcoords->at(j) = vsg::vec2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y);
                }
                else
                {
                    texcoords->at(j) = vsg::vec2(0,0);
                }

                if (mesh->mBitangents)
                {
                    bitangents->at(j) = vsg::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z);
                }
                else
                {
                    bitangents->at(j) = vsg::vec3(1,1,1);
                }

                if (mesh->mTangents)
                {
                    tangents->at(j) = vsg::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z);
                }
                else
                {
                    tangents->at(j) = vsg::vec3(1,1,1);
                }
            }

            for (unsigned int j=0; j<mesh->mNumFaces; ++j)
            {
                const auto &face = mesh->mFaces[j];

                for (unsigned int k=0; k<face.mNumIndices; ++k)
                    indices.push_back(face.mIndices[k]);
            }

            vsg::ref_ptr<vsg::Data> vsg_indices;

            if (indices.size() < std::numeric_limits<uint16_t>::max())
            {
                auto myindices = vsg::ushortArray::create(indices.size());
                std::copy(indices.begin(), indices.end(), myindices->data());
                vsg_indices = myindices;
            }
            else
            {
                auto myindices = vsg::uintArray::create(indices.size());
                std::copy(indices.begin(), indices.end(), myindices->data());
                vsg_indices = myindices;
            }

            auto stategroup = vsg::StateGroup::create();
            xform->addChild(stategroup);

            //qCDebug(lc) << "Using material:" << scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
            if (mesh->mMaterialIndex < bindDescriptorSets.size())
            {
                auto bindDescriptorSet = bindDescriptorSets[mesh->mMaterialIndex];
                stategroup->add(bindDescriptorSet.first);
                stategroup->add(bindDescriptorSet.second);
            }

            stategroup->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, normals, texcoords, tangents, bitangents}));
            stategroup->addChild(vsg::BindIndexBuffer::create(vsg_indices));
            stategroup->addChild(vsg::DrawIndexed::create(indices.size(), 1, 0, 0, 0));
        }

        nodes.pop();
        for (unsigned int i=0; i<node->mNumChildren; ++i)
            nodes.push({node->mChildren[i], xform});
    }

    root->addChild(scenegraph);

    return root;
}

ReaderWriterAssimp::BindState ReaderWriterAssimp::processMaterials(const aiScene *scene, vsg::PipelineLayout *layout, const vsg::Path &basePath) const
{
    BindState bindDescriptorSets;
    bindDescriptorSets.reserve(scene->mNumMaterials);


    auto getTexture = [this, basePath](const aiScene &scene, aiMaterial &material, aiTextureType type, vsg::ref_ptr<vsg::Data> defaultData = kWhiteData) -> vsg::SamplerImage {
        vsg::SamplerImage sampler;
        aiString texPath;

        if (material.GetTexture(type, 0, &texPath) == AI_SUCCESS)
        {
            if (texPath.data[0] == '*')
            {
                const auto texIndex = std::atoi(texPath.C_Str()+1);
                const auto texture = scene.mTextures[texIndex];

                //qCDebug(lc) << "Handle embedded texture" << texPath.C_Str() << texIndex << texture->achFormatHint << texture->mWidth << texture->mHeight;

                if (texture->mWidth > 0 && texture->mHeight == 0)
                {
                    std::istrstream stream((const char*)texture->pcData, texture->mWidth);

                    //sampler.data = _options->readerWriter->read_cast<vsg::Data>(stream, _options);
                    auto options = vsg::Options::create(*_options);
                    options->extensionHint = texture->achFormatHint;
                    sampler.data = vsg::read_cast<vsg::Data>(stream, options);
                }
            }
            else
            {
                const auto filepath = std::filesystem::absolute(vsg::concatPaths(basePath, vsg::Path{texPath.C_Str()}));
                const auto filename = filepath.generic_string();

                if (sampler.data = vsg::read_cast<vsg::Data>(filename, _options); !sampler.data.valid())
                {
                    std::cerr << "Failed to load texture: " << filename << std::endl;
                }

            }

            sampler.sampler = vsg::Sampler::create();
        }
        else
        {
            sampler.data = defaultData;
            sampler.sampler = vsg::Sampler::create();
        }

        if (!sampler.data.valid())
            sampler.data = defaultData;
        else
        {
            sampler.sampler->maxLod = sampler.data->getLayout().maxNumMipmaps;

            if (sampler.sampler->maxLod <= 1.0)
            {
//                if (texPath.length > 0)
//                    std::cout << "Auto generating mipmaps for texture: " << scene.GetShortFilename(texPath.C_Str()) << std::endl;;

                // Calculate maximum lod level
                auto maxDim = std::max(sampler.data->width(), sampler.data->height());
                sampler.sampler->maxLod = std::floor(std::log2(maxDim));
            }
        }

        return sampler;
    };

    for (unsigned int i=0; i<scene->mNumMaterials; ++i)
    {
        const auto material = scene->mMaterials[i];

//        qCDebug(lc) << "Material:" << material->GetName().C_Str();
//        for (unsigned int j=0; j<material->mNumProperties; ++j)
//        {
//            const auto prop = material->mProperties[j];

//            aiString texpath;
//            material->GetTexture((aiTextureType)prop->mSemantic, 0, &texpath);
//            qCDebug(lc) << "Property" << j << prop->mKey.C_Str() << prop->mSemantic << texpath.C_Str();

//        }

        if (PbrMaterial pbr; aiGetMaterialColor(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, (aiColor4D*)&pbr.albedo) == AI_SUCCESS)
        {
            auto baseColorSampler = getTexture(*scene, *material, aiTextureType_DIFFUSE);
            auto mrSampler = getTexture(*scene, *material, aiTextureType_UNKNOWN);
            auto emissiveSampler = getTexture(*scene, *material, aiTextureType_EMISSIVE);
            auto aoSampler = getTexture(*scene, *material, aiTextureType_LIGHTMAP);
            auto normalSampler = getTexture(*scene, *material, aiTextureType_NORMALS, kNormalData);

            aiGetMaterialFloat(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, &pbr.metallicRoughness.r);
            aiGetMaterialFloat(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, &pbr.metallicRoughness.g);
            aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, (aiColor4D*)&pbr.emissive);

            vsg::DescriptorSetLayoutBindings descriptorBindings{
                {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
            };
            vsg::Descriptors descList;

            auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);
            auto buffer = vsg::DescriptorBuffer::create(vsg::Value<PbrMaterial>::create(pbr), 10);
            descList.push_back(buffer);

            auto diffuseTexture = vsg::DescriptorImage::create(baseColorSampler, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(diffuseTexture);

            auto emissiveTexture = vsg::DescriptorImage::create(emissiveSampler, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(emissiveTexture);

            auto aoTexture = vsg::DescriptorImage::create(aoSampler, 3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(aoTexture);

            auto normalTexture = vsg::DescriptorImage::create(normalSampler, 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(normalTexture);

            auto mrTexture = vsg::DescriptorImage::create(mrSampler, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(mrTexture);

            auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, descList);
            auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptorSet);

            bindDescriptorSets.push_back({_bindPbrPipeline, bindDescriptorSet});


            //qCDebug(lc) << "PbrMaterial:" << pbr.albedo.r << pbr.albedo.g << pbr.albedo.b << pbr.base.a << pbr.emissive.r << pbr.emissive.g << pbr.emissive.b << pbr.emissive.a << pbr.metallicRoughness.r << pbr.metallicRoughness.g;
        }
        else
        {
            Material mat;

            aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, (aiColor4D*)&mat.diffuse);
            aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, (aiColor4D*)&mat.ambient);
            aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, (aiColor4D*)&mat.emissive);
            aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, (aiColor4D*)&mat.specular);

            aiShadingMode shadingModel = aiShadingMode_Phong;
            aiGetMaterialInteger(material, AI_MATKEY_SHADING_MODEL, (int*)&shadingModel);

            unsigned int maxValue = 1;
            float strength = 1.0f;
            if (aiGetMaterialFloatArray(material, AI_MATKEY_SHININESS, &mat.shininess, &maxValue) == AI_SUCCESS)
            {
                maxValue = 1;
                if (aiGetMaterialFloatArray(material, AI_MATKEY_SHININESS_STRENGTH, &strength, &maxValue) == AI_SUCCESS)
                    mat.shininess *= strength;
            }
            else
            {
                mat.shininess = 0.0f;
                mat.specular = vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (mat.shininess < 0.01f)
            {
                mat.shininess = 0.0f;
                mat.specular = vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            auto diffuseSampler = getTexture(*scene, *material, aiTextureType_DIFFUSE);
            auto emissiveSampler = getTexture(*scene, *material, aiTextureType_EMISSIVE);
            auto aoSampler = getTexture(*scene, *material, aiTextureType_LIGHTMAP);
            auto normalSampler = getTexture(*scene, *material, aiTextureType_NORMALS, kNormalData);
            auto mrSampler = getTexture(*scene, *material, aiTextureType_METALNESS);

            vsg::DescriptorSetLayoutBindings descriptorBindings{
                {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
            };
            vsg::Descriptors descList;

            auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);
            auto buffer = vsg::DescriptorBuffer::create(vsg::Value<Material>::create(mat), 10);
            descList.push_back(buffer);

            auto diffuseTexture = vsg::DescriptorImage::create(diffuseSampler, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(diffuseTexture);

            auto emissiveTexture = vsg::DescriptorImage::create(emissiveSampler, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(emissiveTexture);

            auto aoTexture = vsg::DescriptorImage::create(aoSampler, 3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(aoTexture);

            auto normalTexture = vsg::DescriptorImage::create(normalSampler, 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(normalTexture);

            auto mrTexture = vsg::DescriptorImage::create(mrSampler, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            descList.push_back(mrTexture);

            auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, descList);
            auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptorSet);

            bindDescriptorSets.push_back({_bindPhongPipeline, bindDescriptorSet});
        }
    }

    return bindDescriptorSets;
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::read(const vsg::Path &filename, vsg::ref_ptr<const vsg::Options> options) const
{
    Assimp::Importer importer;

    if (const auto ext = vsg::fileExtension(filename); importer.IsExtensionSupported(ext))
    {
        if (auto scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_FlipUVs | aiProcess_OptimizeMeshes | aiProcess_SortByPType | aiProcess_ImproveCacheLocality | aiProcess_GenUVCoords); scene)
        {
            //qCDebug(lc) << "File" << filename.c_str() << "loaded successfully. Num meshes" << scene->mNumMeshes << scene->mNumMaterials << scene->mNumTextures;

            return processScene(scene, vsg::filePath(filename));
        }
    }

    return {};
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::read(std::istream &fin, vsg::ref_ptr<const vsg::Options> options) const
{
    return {};
}
