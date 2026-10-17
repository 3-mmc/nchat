// Deterministic activity timing and send acknowledgement correlation.
// nchat is distributed under the MIT license, see LICENSE for details.

#include <iostream>
#include "uiactivity.h"
#include "unittest.h"

int main()
{
  try
  {
    const UiActivity::Chat alice("Signal_a", "same-id");
    const UiActivity::Chat bob("WhatsAppMd_b", "same-id");
    UiActivity activity;
    unittest::ExpectFalse(activity.HasWork());
    activity.Incoming(alice, 1000);
    const auto firstPulse = activity.GetState(alice, 1000, true).marker;
    unittest::ExpectFalse(firstPulse.empty());
    unittest::ExpectTrue(activity.GetState(bob, 1000, true).marker.empty());
    unittest::ExpectTrue(activity.GetState(alice, 1400, true).marker != firstPulse);
    unittest::ExpectTrue(activity.GetState(alice, 1500, false).marker.empty());
    unittest::ExpectTrue(activity.GetState(alice, 7000, true).marker.empty());
    activity.Prune(7000);
    unittest::ExpectFalse(activity.HasWork());
    activity.Incoming(alice, 8000);
    activity.ClearFresh(alice);
    unittest::ExpectTrue(activity.GetState(alice, 8001, true).marker.empty());

    activity.BeginSend(alice, 1, 10000);
    unittest::ExpectTrue(activity.GetState(alice, 11499, true).label.empty());
    unittest::ExpectEqual(std::string, activity.GetState(alice, 11500, true).label.substr(2), "Sending");
    unittest::ExpectTrue(activity.GetState(alice, 11500, true).marker !=
                         activity.GetState(alice, 11750, true).marker);
    unittest::ExpectEqual(std::wstring, activity.GetState(alice, 11750, false).marker, L">");
    unittest::ExpectEqual(std::string, activity.GetState(alice, 25000, false).label, "> Still sending");
    unittest::ExpectFalse(activity.FinishSend(bob, 1, true, 26000));
    unittest::ExpectFalse(activity.FinishSend(alice, 999, true, 26000));
    unittest::ExpectTrue(activity.HasWork());
    unittest::ExpectTrue(activity.FinishSend(alice, 1, true, 26000));
    unittest::ExpectEqual(std::string, activity.GetState(alice, 26000, true).label, "Sent");
    unittest::ExpectFalse(activity.FinishSend(alice, 1, false, 26100)); // duplicate acknowledgement
    unittest::ExpectTrue(activity.GetState(alice, 28000, true).label.empty());
    activity.Prune(28000);
    unittest::ExpectFalse(activity.HasWork());

    // Fast sends do not flash a spinner or a Sent badge.
    activity.BeginSend(alice, 2, 30000);
    unittest::ExpectTrue(activity.FinishSend(alice, 2, true, 30100));
    unittest::ExpectTrue(activity.GetState(alice, 30100, true).label.empty());

    // Concurrent sends, including duplicate text, have independent IDs.
    activity.BeginSend(alice, 3, 40000);
    activity.BeginSend(alice, 4, 40100);
    activity.BeginSend(bob, 5, 40200);
    unittest::ExpectTrue(activity.GetState(alice, 42000, true).label.find("(2)") != std::string::npos);
    unittest::ExpectTrue(activity.FinishSend(alice, 4, true, 42100)); // out of order
    unittest::ExpectTrue(activity.GetState(alice, 42100, true).label.find("Sending") != std::string::npos);
    unittest::ExpectTrue(activity.FinishSend(alice, 3, false, 42200));
    unittest::ExpectEqual(std::wstring, activity.GetState(alice, 42200, false).marker, L"!");
    unittest::ExpectEqual(std::string, activity.GetState(alice, 42200, false).label, "Send failed");
    unittest::ExpectTrue(activity.GetState(bob, 42200, true).label.find("Sending") != std::string::npos);
    activity.Prune(999999);
    unittest::ExpectEqual(std::string, activity.GetState(alice, 999999, true).label, "Send failed");
    unittest::ExpectTrue(activity.GetState(bob, 999999, true).label.find("Still sending") != std::string::npos);

    const UiActivity::Chat moved("WhatsAppMd_b", "new-id");
    activity.MoveChat(bob, moved);
    unittest::ExpectTrue(activity.GetState(bob, 1000000, true).label.empty());
    unittest::ExpectTrue(activity.GetState(moved, 1000000, true).label.find("Still sending") != std::string::npos);
    unittest::ExpectTrue(activity.FinishSend(bob, 5, true, 1000001)); // original request ID/chat still matches
    unittest::ExpectEqual(std::string, activity.GetState(moved, 1000001, true).label, "Sent");
    activity.BeginSend(alice, 6, 1000002); // a new attempt clears the previous error
    unittest::ExpectTrue(activity.GetState(alice, 1000002, true).label.empty());
    activity.RemoveChat(alice);
    unittest::ExpectFalse(activity.FinishSend(alice, 6, false, 1000003));
    unittest::ExpectTrue(activity.GetState(alice, 1000003, true).marker.empty());
    activity.Prune(1002001);
    unittest::ExpectFalse(activity.HasWork());
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
