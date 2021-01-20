#include "ReaderWriterKTX.h"

#include "ktx.h"
#include "ktxvulkan.h"

#include <filesystem>
#include <QLoggingCategory>


namespace {
static QLoggingCategory lc("readerwriter.ktx");

vsg::ref_ptr<vsg::Data> readKtx(ktxTexture *texture)
{
    vsg::ref_ptr<vsg::Data> data;
    const auto width = texture->baseWidth;
    const auto height = texture->baseHeight;
    const auto mipLevels = texture->numLevels;
    const auto textureData = ktxTexture_GetData(texture);
    const auto textureSize = ktxTexture_GetSize(texture);
    const auto format = ktxTexture_GetVkFormat(texture);

    qCDebug(lc) << __func__ << width << height << mipLevels << textureData << textureSize << format;

    if (format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM)
    {
        auto raw = new uint8_t[textureSize];
        auto pair = std::pair{texture, raw};

        auto readCB = [](int miplevel, int face, int width, int height, int depth, ktx_uint32_t faceLodSize, void* pixels, void* userdata) -> KTX_error_code {

            auto [texture, raw] = *((std::pair<ktxTexture*, uint8_t*>*)userdata);

            if (ktx_size_t offset = 0; ktxTexture_GetImageOffset(texture, miplevel, 0, face, &offset) == KTX_SUCCESS)
            {
                std::memcpy(raw + offset, pixels, faceLodSize);
                return KTX_SUCCESS;
            }

            return KTX_INVALID_OPERATION;
        };

        ktxTexture_IterateLoadLevelFaces(texture, readCB, &pair);

        auto layout = vsg::Data::Layout{format};
        layout.maxNumMipmaps = mipLevels;

        data = vsg::ubvec4Array2D::create(width, height, reinterpret_cast<vsg::ubvec4*>(raw), layout);
    }

    ktxTexture_Destroy(texture);

    return data;
}

}


ReaderWriterKTX::ReaderWriterKTX()
    : _supportedExtensions{"ktx"}
{
}

vsg::ref_ptr<vsg::Object> ReaderWriterKTX::read(const vsg::Path &filename, vsg::ref_ptr<const vsg::Options> options) const
{
    if (!std::filesystem::exists(filename))
        return {};

    if (const auto ext = vsg::fileExtension(filename); _supportedExtensions.count(ext) == 0)
        return {};

    if (ktxTexture *texture{nullptr}; ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &texture) == KTX_SUCCESS)
        return readKtx(texture);

    return {};
}

vsg::ref_ptr<vsg::Object> ReaderWriterKTX::read(std::istream &fin, vsg::ref_ptr<const vsg::Options> options) const
{
    if (_supportedExtensions.count(options->extensionHint) == 0)
        return {};

    std::string buffer(1<<16, 0); // 64kB
    std::string input;

    while (!fin.eof())
    {
        fin.read(&buffer[0], buffer.size());
        const auto bytes_readed = fin.gcount();
        input.append(&buffer[0], bytes_readed);
    }

    if (ktxTexture *texture{nullptr}; ktxTexture_CreateFromMemory((const ktx_uint8_t*)input.data(), input.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &texture) == KTX_SUCCESS)
        return readKtx(texture);

    return {};
}
