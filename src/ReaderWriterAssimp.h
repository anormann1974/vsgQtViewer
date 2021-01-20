#pragma once

#include <vsg/io/ReaderWriter.h>

struct aiScene;

namespace vsg {
class PipelineLayout;
class ShaderStage;
class BindGraphicsPipeline;
}

class ReaderWriterAssimp : public vsg::Inherit<vsg::ReaderWriter, ReaderWriterAssimp>
{
public:
    ReaderWriterAssimp();

    vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {}) const override;
    vsg::ref_ptr<vsg::Object> read(std::istream& fin, vsg::ref_ptr<const vsg::Options> options = {}) const override;

private:

    using StateCommandPtr = vsg::ref_ptr<vsg::StateCommand>;
    using State = std::pair<StateCommandPtr, StateCommandPtr>;
    using BindState = std::vector<State>;

    void createGraphicsPipelines();
    vsg::ref_ptr<vsg::Object> processScene(const aiScene *scene, const vsg::Path &basePath) const;
    BindState processMaterials(const aiScene *scene, vsg::PipelineLayout *layout, const vsg::Path &basePath) const;

    std::vector<vsg::ref_ptr<vsg::ShaderStage>> _shaders, _pbrShaders;
    vsg::ref_ptr<vsg::Options> _options;
    vsg::ref_ptr<vsg::BindGraphicsPipeline> _bindPhongPipeline, _bindPbrPipeline;
    vsg::ref_ptr<vsg::BindDescriptorSet> _bindDefaultPhongDescriptorSet;
};

