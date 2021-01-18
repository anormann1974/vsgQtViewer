#include "ReaderWriterAssimp.h"
#include "ReaderWriterStbi.h"

#include <stack>
#include <filesystem>
#include <strstream>

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

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>

#include <QLoggingCategory>
#include <QFile>

namespace {
static QLoggingCategory lc("ReaderWriterAssimp");

struct Material {
    vsg::vec4 ambient{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 diffuse{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 specular{0.0f, 0.0f, 0.0f, 1.0f};
    vsg::vec4 emissive{1.0f, 1.0f, 1.0f, 1.0f};
    float shininess{0.0f};
};

struct PbrMaterial {
    vsg::vec4 base{0.0, 0.0, 0.0, 1.0};
};

static vsg::vec4 kBlackColor{0.0, 0.0, 0.0, 0.0};
static vsg::vec4 kWhiteColor{1.0, 1.0, 1.0, 1.0};

vsg::ref_ptr<vsg::Data> createTexture(const vsg::vec4 &color)
{
    auto vsg_data = vsg::vec4Array2D::create(1, 1, vsg::Data::Layout{VK_FORMAT_R32G32B32A32_SFLOAT});
    std::fill(vsg_data->begin(), vsg_data->end(), color);
    return vsg_data;
}

static auto kWhiteData = createTexture(kWhiteColor);
static auto kBlackData = createTexture(kBlackColor);

}

ReaderWriterAssimp::ReaderWriterAssimp()
    : _options{vsg::Options::create(ReaderWriterStbi::create())}
{
    _shaders.reserve(5);    

    auto readSpvFile = [this](const QString &filename, VkShaderStageFlagBits stage) {
        if (QFile file(filename); file.open(QIODevice::ReadOnly))
        {
            const auto content = file.readAll();
            vsg::ShaderModule::SPIRV spirv(content.size() / sizeof(vsg::ShaderModule::SPIRV::value_type));

            std::memcpy(spirv.data(), content.constData(), content.size());

            if (auto shader = vsg::ShaderStage::create(stage, "main", spirv); shader.valid())
                _shaders.push_back(shader);

            file.close();
        }
    };

    auto readSpvSingleFile = [this](const QString &filename) {
        if (QFile file(filename); file.open(QIODevice::ReadOnly))
        {
            const auto content = file.readAll();
            vsg::ShaderModule::SPIRV spirv(content.size() / sizeof(vsg::ShaderModule::SPIRV::value_type));

            std::memcpy(spirv.data(), content.constData(), content.size());

            if (auto shader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", spirv); shader.valid())
                _shaders.push_back(shader);

            if (auto shader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", spirv); shader.valid())
                _shaders.push_back(shader);

            file.close();
        }
    };

    auto readGlslFile = [this](const QString &filename, VkShaderStageFlagBits stage) {
        if (QFile file(filename); file.open(QIODevice::ReadOnly))
        {
            const auto content = file.readAll();

            if (auto shader = vsg::ShaderStage::create(stage, "main", content.toStdString()); shader.valid())
                _shaders.push_back(shader);

            file.close();
        }
    };

    readGlslFile(":/shader.vert", VK_SHADER_STAGE_VERTEX_BIT);
    readGlslFile(":/shader.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
//    readSpvFile(":/vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
//    readSpvFile(":/fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
//    readSpvSingleFile(":/shader.spv");

    qCDebug(lc) << __func__ << _shaders.size();
}

auto ReaderWriterAssimp::createGraphicsPipeline() const
{
    // set up graphics pipeline
    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
    };

    vsg::VertexInputState::Bindings vertexBindingsDescriptions{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // vertex data
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // normal data
        VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}  // texcoord data
    };

    vsg::VertexInputState::Attributes vertexAttributeDescriptions{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // vertex data
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // normal data
        VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT, 0}  // texcoord data
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
    auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(pipeline);

    // create texture image and associated DescriptorSets and binding
    auto sampler = vsg::Sampler::create();
    auto texture = vsg::DescriptorImage::create(sampler, kWhiteData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto emissiveTexture = vsg::DescriptorImage::create(sampler, kWhiteData, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto material = vsg::DescriptorBuffer::create(vsg::Value<Material>::create(Material()), 10);

    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{material, texture, emissiveTexture});
    auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);

    return std::pair{bindGraphicsPipeline, bindDescriptorSet};
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::processScene(const aiScene *scene, const vsg::Path &basePath) const
{
    // Process materials

    auto [bindGraphicsPipeline, bindDescriptorSet] = createGraphicsPipeline();

    auto pipelineLayout = bindGraphicsPipeline->pipeline->layout;
    //auto textures = processTextures(scene, basePath);
    auto bindDescriptorSets = processMaterials(scene, pipelineLayout.get(), basePath);

    auto scenegraph = vsg::StateGroup::create();
    scenegraph->add(bindGraphicsPipeline);
    scenegraph->add(bindDescriptorSet);

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

            qCDebug(lc) << "Using material:" << scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
            if (mesh->mMaterialIndex < bindDescriptorSets.size())
            {
                auto bindDescriptorSet = bindDescriptorSets[mesh->mMaterialIndex];
                stategroup->add(bindDescriptorSet);
            }

            stategroup->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, normals, texcoords}));
            stategroup->addChild(vsg::BindIndexBuffer::create(vsg_indices));
            stategroup->addChild(vsg::DrawIndexed::create(indices.size(), 1, 0, 0, 0));
        }

        nodes.pop();
        for (unsigned int i=0; i<node->mNumChildren; ++i)
            nodes.push({node->mChildren[i], xform});
    }

    return scenegraph;
}

ReaderWriterAssimp::BindDescriptorSets ReaderWriterAssimp::processMaterials(const aiScene *scene, vsg::PipelineLayout *layout, const vsg::Path &basePath) const
{
    BindDescriptorSets bindDescriptorSets;
    bindDescriptorSets.reserve(scene->mNumMaterials);

    auto getTexture = [this, basePath](const aiScene &scene, const aiMaterial &material, aiTextureType type) -> vsg::SamplerImage {
        vsg::SamplerImage sampler;

        if (aiString texPath; material.GetTexture(type, 0, &texPath) == AI_SUCCESS)
        {
            if (texPath.data[0] == '*')
            {
                const auto texIndex = std::atoi(texPath.C_Str()+1);
                const auto texture = scene.mTextures[texIndex];

                //qCDebug(lc) << "Handle embedded texture" << texPath.C_Str() << texIndex << texture->achFormatHint << texture->mWidth << texture->mHeight;

                if (texture->mWidth > 0 && texture->mHeight == 0)
                {
                    std::istrstream stream((const char*)texture->pcData, texture->mWidth);

                    sampler.data = _options->readerWriter->read_cast<vsg::Data>(stream, _options);
                }
            }
            else
            {
                const auto filepath = std::filesystem::absolute(vsg::concatPaths(basePath, vsg::Path{texPath.C_Str()}));
                const auto filename = filepath.generic_string();

                sampler.data = vsg::read_cast<vsg::Data>(filename, _options);
            }

            sampler.sampler = vsg::Sampler::create();
        }
        else
        {
            sampler.data = kWhiteData;
            sampler.sampler = vsg::Sampler::create();
        }

        if (!sampler.data.valid())
            sampler.data = kWhiteData;
        else
        {
            // Calculate maximum lod level
            auto maxDim = std::max(sampler.data->width(), sampler.data->height());
            sampler.sampler->maxLod = std::log2(maxDim);
        }

        return sampler;
    };

    for (unsigned int i=0; i<scene->mNumMaterials; ++i)
    {
        const auto material = scene->mMaterials[i];        

//        for (unsigned int j=0; j<material->mNumProperties; ++j)
//        {
//            const auto prop = material->mProperties[j];
//            qCDebug(lc) << "Property" << j << prop->mKey.C_Str();
//        }

//        if (PbrMaterial pbr; aiGetMaterialColor(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, (aiColor4D*)&pbr.base) == AI_SUCCESS)
//        {
//            unsigned int unit{10};

//            qCDebug(lc) << "PbrMaterial";

//            for (const auto textureType : {aiTextureType_DIFFUSE, aiTextureType_UNKNOWN, aiTextureType_EMISSIVE, aiTextureType_LIGHTMAP, aiTextureType_NORMALS})
//            {
//                if (aiString path; material->GetTexture(textureType, 0, &path, nullptr, &unit) == AI_SUCCESS)
//                {
//                    qCDebug(lc).nospace() << "\t" << textureType << " texture: " << unit << " " << path.C_Str();
//                }
//            }
//            //qCDebug(lc) << "Textures:" << material->GetTextureCount(aiTextureType_DIFFUSE) << material->GetTextureCount(aiTextureType_UNKNOWN) << material->GetTextureCount(aiTextureType_EMISSIVE) << material->GetTextureCount(aiTextureType_LIGHTMAP) << material->GetTextureCount(aiTextureType_NORMALS);
//        }
//        else
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

            vsg::DescriptorSetLayoutBindings descriptorBindings{
                {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
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

            auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, descList);
            auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptorSet);

            bindDescriptorSets.push_back(bindDescriptorSet);
        }
    }

    return bindDescriptorSets;
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::read(const vsg::Path &filename, vsg::ref_ptr<const vsg::Options> options) const
{
    Assimp::Importer importer;

    if (const auto ext = vsg::fileExtension(filename); importer.IsExtensionSupported(ext))
    {
        if (auto scene = importer.ReadFile(filename, aiProcess_Triangulate); scene)
        {
            qCDebug(lc) << "File" << filename.c_str() << "loaded successfully. Num meshes" << scene->mNumMeshes << scene->mNumMaterials << scene->mNumTextures;

            return processScene(scene, vsg::filePath(filename));
        }
    }

    return {};
}

vsg::ref_ptr<vsg::Object> ReaderWriterAssimp::read(std::istream &fin, vsg::ref_ptr<const vsg::Options> options) const
{
    return {};
}
