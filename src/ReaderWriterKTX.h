#pragma once

#include <vsg/io/ReaderWriter.h>
#include <unordered_set>

class ReaderWriterKTX : public vsg::Inherit<vsg::ReaderWriter, ReaderWriterKTX>
{
public:
    ReaderWriterKTX();

    vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {}) const override;
    vsg::ref_ptr<vsg::Object> read(std::istream& fin, vsg::ref_ptr<const vsg::Options> options = {}) const override;

private:
    std::unordered_set<std::string> _supportedExtensions;
};

