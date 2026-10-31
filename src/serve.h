// Headless stdio frontend. See doc/serve.md for the wire protocol.
#pragma once

#include <memory>
#include <string>

#include "protocol.h"

class Serve
{
public:
  Serve();
  ~Serve();
  void AddProtocol(const std::string& p_Name, std::shared_ptr<Protocol> p_Protocol);
  void AddSkipped(const std::string& p_Profile, const std::string& p_Why);
  void Run();

private:
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};
