#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <windows.h>

#include "resource.hpp"

std::string random_filename() {
   const static std::string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
   std::string result;

   for (std::size_t i=0; i<8; ++i)
      result.push_back(alphabet[std::rand() % alphabet.size()]);

   return result;
}

std::filesystem::path create_temp_path() {
   std::filesystem::path result = fmt::format("{}\\rps-{}", std::filesystem::temp_directory_path().string(), random_filename());

   if (!std::filesystem::exists(result) && !std::filesystem::create_directories(result))
      throw std::runtime_error(fmt::format("couldn't create directories: {}", result.string()));

   return result;
}

std::string get_template() {
   auto name = (LPCSTR)IDI_TEMPLATE;
   auto type = "SED";
   auto res = FindResourceA(nullptr, name, type);

   if (res == nullptr)
      throw std::runtime_error(fmt::format("FindResource failed: win32 error {}", GetLastError()));

   auto size = SizeofResource(nullptr, res);
   auto load = LoadResource(nullptr, res);

   if (load == nullptr)
      throw std::runtime_error(fmt::format("LoadResource failed: win32 error {}", GetLastError()));

   auto ptr = reinterpret_cast<std::uint8_t *>(LockResource(load));

   return std::string(ptr, ptr+size);
}

void create_cabinet(const std::filesystem::path &installer,
                    std::optional<std::filesystem::path> post_exec,
                    const std::filesystem::path &outfile)
{
   auto sed_template = get_template();
   auto cab_directory = create_temp_path();

   try {
      auto installer_random = fmt::format("{}{}", random_filename(), installer.extension().string());
      std::string post_random;

      if (post_exec.has_value())
         post_random = fmt::format("{}{}", random_filename(), post_exec->extension().string());

      auto full_temp_installer = cab_directory / installer_random;
      std::filesystem::copy(installer, full_temp_installer);
   
      std::filesystem::path full_temp_post;

      if (post_random.size() > 0)
      {
         full_temp_post = cab_directory / post_random;
         std::filesystem::copy(*post_exec, full_temp_post);
      }

      auto payload_file_strings = fmt::format("FILE0={}", installer_random);
      auto payload_source_files = std::string("%FILE0%=");

      if (post_random.size() > 0)
      {
         payload_file_strings = fmt::format("{}\nFILE1={}", payload_file_strings, post_random);
         payload_source_files = fmt::format("{}\n%FILE1%=", payload_source_files);
      }
      else
         post_random = "<None>";
   
      auto formatted_template = fmt::format(sed_template,
                                            random_filename(),
                                            installer_random,
                                            post_random,
                                            cab_directory.string(),
                                            payload_file_strings,
                                            payload_source_files,
                                            outfile.string());
   
      auto temp_sed_filename = fmt::format("{}\\metadata.sed", cab_directory.string());
   
      std::ofstream sed_file(temp_sed_filename);
      sed_file << formatted_template;
      sed_file.close();

      STARTUPINFOA startup_info;
      std::memset(&startup_info, 0, sizeof(STARTUPINFOA));
      startup_info.cb = sizeof(STARTUPINFOA);
   
      PROCESS_INFORMATION process_information;
      std::memset(&process_information, 0, sizeof(PROCESS_INFORMATION));
   
      auto command_line = std::string("iexpress /N metadata.sed /Q");
      auto command_line_vec = std::vector(command_line.c_str(), command_line.c_str()+command_line.size()+1);

      if (!CreateProcessA(nullptr,
                          command_line_vec.data(),
                          nullptr,
                          nullptr,
                          FALSE,
                          0,
                          nullptr,
                          cab_directory.string().c_str(),
                          &startup_info,
                          &process_information))
         throw std::runtime_error(fmt::format("CreateProcess failed: win32 error {}", GetLastError()));

      WaitForSingleObject(process_information.hProcess, INFINITE);

      DWORD exit_code = 0;
      GetExitCodeProcess(process_information.hProcess, &exit_code);

      if (exit_code != 0)
         throw std::runtime_error(fmt::format("iexpress failed: exit code {}", exit_code));
   
      CloseHandle(process_information.hProcess);
      CloseHandle(process_information.hThread);

      std::filesystem::remove_all(cab_directory);
   }
   catch (std::exception &exc) {
      std::filesystem::remove_all(cab_directory);
      throw exc;
   }
}

void create_payload(const std::vector<std::filesystem::path> &payloads, const std::filesystem::path &outfile) {
   std::filesystem::path outfile_absolute = outfile;

   if (!outfile_absolute.has_root_path())
      outfile_absolute = std::filesystem::current_path() / outfile_absolute;
   
   if (payloads.size() == 1)
      create_cabinet(payloads[0], std::nullopt, outfile_absolute);
   else if (payloads.size() == 2)
      create_cabinet(payloads[0], payloads[1], outfile_absolute);
   else
   {
      auto temp_path = create_temp_path();

      try {
         auto init_layer_filename = fmt::format("{}/layer.exe", temp_path.string());
         create_cabinet(payloads[0], payloads[1], init_layer_filename);

         for (std::size_t i=2; i<payloads.size(); ++i)
         {
            auto new_layer_filename = fmt::format("{}/new_layer.exe", temp_path.string());
            create_cabinet(init_layer_filename, payloads[i], new_layer_filename);
            std::filesystem::rename(new_layer_filename, init_layer_filename);
         }

         std::filesystem::copy(init_layer_filename, outfile_absolute);
         std::filesystem::remove_all(temp_path);
      }
      catch (std::exception &exc) {
         std::filesystem::remove_all(temp_path);
         throw exc;
      }
   }
}

int main(int argc, char *argv[])
{
   std::srand(std::time(nullptr));
   
   try {
      if (argc < 2)
         throw std::runtime_error("no output filename provided");

      if (argc < 3)
         throw std::runtime_error("no payloads provided");
      
      std::vector<std::filesystem::path> payloads;

      for (std::size_t i=2; i<static_cast<std::size_t>(argc); ++i)
         payloads.push_back(std::string(argv[i]));

      create_payload(payloads, argv[1]);
   }
   catch (std::exception &e)
   {
      std::cerr << "error: " << e.what() << std::endl;
      return 1;
   }

   return 0;
}
