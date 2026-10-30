// Host tests for saved WiFi selection. Run: firmware/test/host/run.sh
#include "setup_form.h"
#include "wifi_pick.h"

#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool sameNetworks(const std::vector<setup_form::Network>& a,
                         const std::vector<setup_form::Network>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].ssid != b[i].ssid || a[i].pass != b[i].pass) return false;
    return true;
}

static void selection() {
    const std::vector<setup_form::Network> known = {
        {"Home", "homepass"}, {"Cafe", ""}, {"Lab", "labpassword"}};
    CHECK(wifi_pick::pick(known, {{"Home", -80}, {"Cafe", -40}, {"Lab", -60},
                                  {"Park", -10}}) == 1);
    CHECK(wifi_pick::pick(known, {{"Lab", -90}}) == 2);
    // The strongest duplicate wins, regardless of scan order.
    CHECK(wifi_pick::pick(known, {{"Cafe", -30}, {"Home", -50}, {"Cafe", -80}}) == 1);
    CHECK(wifi_pick::pick(known, {{"Cafe", -80}, {"Home", -50}, {"Cafe", -30}}) == 1);
    // Ties use saved order, not scan order (including duplicate entries).
    CHECK(wifi_pick::pick(known, {{"Lab", -40}, {"Cafe", -40}, {"Home", -40}}) == 0);
    CHECK(wifi_pick::pick(known, {{"Lab", -40}, {"Cafe", -80}, {"Cafe", -40}}) == 1);
    CHECK(wifi_pick::pick(known, {{"Park", -20}}) == -1);
    CHECK(wifi_pick::pick(known, {{"home", -20}, {"Home ", -10}}) == -1);
    CHECK(wifi_pick::pick(known, {}) == -1);
    CHECK(wifi_pick::pick({}, {{"Home", -40}}) == -1);
    CHECK(wifi_pick::pick({}, {}) == -1);
}

static void joinOrder() {
    const std::vector<setup_form::Network> known = {
        {"Home", "homepass"}, {"Cafe", ""}, {"Lab", "labpassword"},
        {"Park", "parkpass"}, {"Studio", "studiopass"}};
    const auto checkOrder = [&](const std::vector<wifi_pick::Seen>& scan,
                                const std::vector<int>& expected) {
        const auto actual = wifi_pick::order(known, scan);
        CHECK(actual == expected);
        CHECK(actual.size() == known.size());
        // Every saved index occurs exactly once, including unseen networks.
        for (size_t i = 0; i < known.size(); ++i) {
            size_t count = 0;
            for (int idx : actual) if (idx == static_cast<int>(i)) ++count;
            CHECK(count == 1);
        }
    };
    checkOrder({{"Home", -80}, {"Cafe", -40}, {"Lab", -60},
                {"Park", -30}, {"Studio", -50}, {"Garden", -10}}, {3, 1, 4, 2, 0});
    // Strongest duplicate counts whether it appears first or last.
    checkOrder({{"Lab", -30}, {"Cafe", -50}, {"Lab", -80}}, {2, 1, 0, 3, 4});
    checkOrder({{"Lab", -80}, {"Cafe", -50}, {"Lab", -30}}, {2, 1, 0, 3, 4});
    // Equal strengths use saved order even when the scan is reversed.
    checkOrder({{"Studio", -40}, {"Park", -40}, {"Lab", -40},
                {"Cafe", -40}, {"Home", -40}}, {0, 1, 2, 3, 4});
    checkOrder({{"Studio", -40}, {"Cafe", -80}, {"Cafe", -40}}, {1, 4, 0, 2, 3});
    // Unseen entries follow even a weak signal, in saved order.
    checkOrder({{"Park", -100}}, {3, 0, 1, 2, 4});
    checkOrder({{"Garden", -20}, {"home", -10}, {"Home ", -5}}, {0, 1, 2, 3, 4});
    checkOrder({}, {0, 1, 2, 3, 4});
    CHECK(wifi_pick::order({}, {{"Home", -40}}).empty());
    CHECK(wifi_pick::order({}, {}).empty());
}

static void promotion() {
    const std::vector<setup_form::Network> original = {
        {"Home", "homepass"}, {"Cafe", ""}, {"Lab", "labpassword"}, {"Park", "parkpass"}};
    auto known = original;
    CHECK(wifi_pick::promote(known, 2));
    CHECK(sameNetworks(known, {{"Lab", "labpassword"}, {"Home", "homepass"},
                              {"Cafe", ""}, {"Park", "parkpass"}}));
    known = original;
    CHECK(wifi_pick::promote(known, 3));
    CHECK(sameNetworks(known, {{"Park", "parkpass"}, {"Home", "homepass"},
                              {"Cafe", ""}, {"Lab", "labpassword"}}));
    known = original;
    CHECK(!wifi_pick::promote(known, 0));
    CHECK(sameNetworks(known, original));
    for (int idx : {-1, static_cast<int>(original.size()),
                    std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        CHECK(!wifi_pick::promote(known, idx));
        CHECK(sameNetworks(known, original));
    }
    known = {{"Home", "homepass"}};
    CHECK(!wifi_pick::promote(known, 0));
    CHECK(sameNetworks(known, {{"Home", "homepass"}}));
    known.clear();
    CHECK(!wifi_pick::promote(known, 0));
    CHECK(!wifi_pick::promote(known, -1));
    CHECK(known.empty());
}

int main() {
    selection();
    joinOrder();
    promotion();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("wifi_pick: all checks passed\n");
    return 0;
}
