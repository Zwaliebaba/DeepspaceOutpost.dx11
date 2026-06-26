#pragma once

namespace Neuron
{
  using byte_buffer_t = std::vector<uint8_t>;

  class FileSys
  {
    public:
      static void SetHomeDirectory(const std::wstring& _path) { m_homeDir = _path + L"\\Assets\\"; }
      [[nodiscard]] static std::wstring GetHomeDirectory() { return m_homeDir; }

    protected:
      inline static std::wstring m_homeDir;
  };

  class BinaryFile : public FileSys
  {
    public:
      [[nodiscard]] static byte_buffer_t ReadFile(const std::wstring& _fileName);
  };

  class TextFile : public FileSys
  {
    public:
      [[nodiscard]] static std::wstring ReadFile(const std::wstring& _fileName);
  };
}
