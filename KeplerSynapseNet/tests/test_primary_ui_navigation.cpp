#include "tui/primary_ui_spec.h"

#include <cassert>

static void testDashboardKeyRouting() {
    using synapse::tui::primary_ui::DashboardRoute;
    using synapse::tui::primary_ui::routeDashboardKey;

    assert(routeDashboardKey('A') == DashboardRoute::AI_MINING);
    assert(routeDashboardKey('a') == DashboardRoute::AI_MINING);
    assert(routeDashboardKey('3') == DashboardRoute::AI_QUERY);
    assert(routeDashboardKey('Q') == DashboardRoute::QUIT);
    assert(routeDashboardKey('q') == DashboardRoute::QUIT);
    assert(routeDashboardKey('1') == DashboardRoute::NONE);
}

static void testMiningPageRouting() {
    using synapse::tui::primary_ui::MiningPage;
    using synapse::tui::primary_ui::routeMiningPageKey;

    bool handled = false;

    auto page = routeMiningPageKey(MiningPage::OVERVIEW, '2', &handled);
    assert(handled);
    assert(page == MiningPage::NETWORK_MINING);

    page = routeMiningPageKey(MiningPage::NETWORK_MINING, '9', &handled);
    assert(handled);
    assert(page == MiningPage::STORAGE_RECOVERY);

    page = routeMiningPageKey(MiningPage::OVERVIEW, 'H', &handled);
    assert(handled);
    assert(page == MiningPage::STORAGE_RECOVERY);

    page = routeMiningPageKey(MiningPage::STORAGE_RECOVERY, 'l', &handled);
    assert(handled);
    assert(page == MiningPage::OVERVIEW);

    page = routeMiningPageKey(MiningPage::CONTRIBUTIONS, 'z', &handled);
    assert(!handled);
    assert(page == MiningPage::CONTRIBUTIONS);
}

int main() {
    testDashboardKeyRouting();
    testMiningPageRouting();
    return 0;
}
