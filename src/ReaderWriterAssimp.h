#pragma once

#include <vsg/io/ReaderWriter.h>

struct aiScene;

namespace vsg {
class PipelineLayout;
class ShaderStage;
}

class ReaderWriterAssimp : public vsg::Inherit<vsg::ReaderWriter, ReaderWriterAssimp>
{
public:
    ReaderWriterAssimp();

    vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {}) const override;
    vsg::ref_ptr<vsg::Object> read(std::istream& fin, vsg::ref_ptr<const vsg::Options> options = {}) const override;

private:

    using BindDescriptorSets = std::vector<vsg::ref_ptr<vsg::StateCommand>>;

    auto createGraphicsPipeline() const;
    vsg::ref_ptr<vsg::Object> processScene(const aiScene *scene, const vsg::Path &basePath) const;
    BindDescriptorSets processMaterials(const aiScene *scene, vsg::PipelineLayout *layout, const vsg::Path &basePath) const;

    std::vector<vsg::ref_ptr<vsg::ShaderStage>> _shaders;
    vsg::ref_ptr<vsg::Options> _options;
};

