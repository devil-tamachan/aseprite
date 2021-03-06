// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifndef APP_CMD_ADD_CEL_H_INCLUDED
#define APP_CMD_ADD_CEL_H_INCLUDED
#pragma once

#include "app/cmd.h"
#include "app/cmd/with_cel.h"
#include "app/cmd/with_layer.h"

#include <sstream>

namespace doc {
  class Cel;
  class Layer;
}

namespace app {
namespace cmd {
  using namespace doc;

  class AddCel : public Cmd
               , public WithLayer
               , public WithCel {
  public:
    AddCel(Layer* layer, Cel* cel);

  protected:
    void onExecute() override;
    void onUndo() override;
    void onRedo() override;
    size_t onMemSize() const override {
      return sizeof(*this) +
        (size_t)const_cast<std::stringstream*>(&m_stream)->tellp();
    }

  private:
    void addCel(Layer* layer, Cel* cel);
    void removeCel(Layer* layer, Cel* cel);

    std::stringstream m_stream;
  };

} // namespace cmd
} // namespace app

#endif
