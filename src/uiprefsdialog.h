// uiprefsdialog.h
//
// Preferences dialog: media display, chat display options and accounts
// (status, order, pinned-only), stored in ui.conf / app.conf.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "uilistdialog.h"

class UiPrefsDialog : public UiListDialog
{
public:
  UiPrefsDialog(const UiDialogParams& p_Params);
  virtual ~UiPrefsDialog();

  virtual void KeyHandler(wint_t p_Key);

protected:
  void Draw() override;
  int GetListWidth() const;
  void DrawPreview(int p_ListWidth);

  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();
  virtual void UpdateList();
  virtual void UpdateFooter();

private:
  enum ItemType
  {
    ItemHeader = 0,
    ItemChoice,
    ItemFolder,
    ItemAccount,
  };

  struct PrefItem
  {
    ItemType type = ItemHeader;
    std::string label;
    std::string param;
    bool isAppConfig = false; // app.conf instead of ui.conf
    std::vector<std::pair<std::string, std::string>> choices; // value, display
    std::string profileId;
  };

  void BuildItems();
  std::string GetValue(const PrefItem& p_Item);
  void SetValue(const PrefItem& p_Item, const std::string& p_Value);
  void Cycle(int p_Delta);
  void EditFolder(const PrefItem& p_Item);

private:
  std::vector<PrefItem> m_PrefItems;
  int64_t m_LastStateCheck = 0;
  std::string m_LastStates;
};
