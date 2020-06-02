#include <iostream>
#include <array>
#include <cmath>
#include <algorithm>
#include "../bola_basic.hh"

using namespace std;
typedef BolaBasic::Encoded Encoded;
typedef BolaBasic::Parameters Parameters;

// Size units don't matter for paper's definition of utility
template<typename SizeArr>
double utility_paper(unsigned i, const SizeArr &sizes) {
    return log(sizes.at(i) / sizes.back());
}

/* Using the sizes, min/max buf (implicit from figures) and utility used for Fig 1/2 in paper: 
 * recover V = 0.93, gp = 5, Fig 1/2 */
void test_paper_ladder() {
    constexpr unsigned NFORMATS = 5;
    const array<double, NFORMATS> bitrates_Mbps = {
        6.000,
        2.962,
        1.427,
        0.688,
        0.331 };

    const array<double, NFORMATS> sizes = {
        18.00,
        8.886,
        4.281,
        2.064,
        0.993 };

    const double p = 3;
    // The paper's static V and gp used for Fig 1/2
    const double V_static = 0.93;
    const double gp_static = 5;
    
    // Use min/max buf from Fig 1/2 (not 3p)
    // Use sizes as doubles to exactly match paper's numbers
    double min_buf_chunks = V_static * (sizes[NFORMATS - 2] * (utility_paper(NFORMATS - 1, sizes) + gp_static) - 
            sizes[NFORMATS - 1] * (utility_paper(NFORMATS - 2, sizes) + gp_static)) / 
                            (sizes[NFORMATS - 2] - sizes[NFORMATS - 1]); 
    cerr << "min_buf_sec " << min_buf_chunks * p << endl;    // should be ~12
    
    double max_buf_chunks = V_static * (utility_paper(0, sizes) + gp_static); 
    cerr << "max_buf_sec " << max_buf_chunks * p << endl;    // should be ~22
    
    /* Fake encoded formats with static sizes, paper's utility */
    vector<Encoded> encoded_formats;
    for (unsigned i = 0; i < NFORMATS; i++) {
        encoded_formats.emplace_back(Encoded { 
                // Use "width" in vf name for plotting
                VideoFormat(to_string(bitrates_Mbps.at(i) * 1000) + "x11-11"), 
                // Size units don't impact BOLA math; just need a size_t
                static_cast<size_t>(sizes.at(i) / 8 * pow(10, 6)), 
                utility_paper(i, sizes) });
    }
    
    YAML::Node config;
    WebSocketClient client(11, "bola_basic", config);
    BolaBasic bola(client, "bola_basic", config);
   
    bola.MIN_BUF_CHUNKS = min_buf_chunks;
    bola.MAX_BUF_CHUNKS = max_buf_chunks;

    // Re-calculate parameters with test min/max/ladder
    // calculate_parameters expects ladders sorted ascending
    sort(encoded_formats.begin(), encoded_formats.end(),
     [](const Encoded& a, const Encoded& b) {
            return a.size < b.size;
     });

    bola.calculate_parameters(encoded_formats);

    /* Recover V and gp from bitrate ladder */
    cerr << "recovered V = " << bola.params.V << " (static V = " << V_static 
         << "), recovered gp = " << bola.params.gp << " (static gp = " << gp_static << ")" << endl;

    /* Recover figures */
    bola.fig_1(encoded_formats, p, 0, "test_paper_ladder");
    bola.fig_2(encoded_formats, p, 0, "test_paper_ladder");
}

/* Using the sizes, SSIMs, min/max buf and utility used to statically calculate parameters in bola_basic:
 * plot Fig1/2 equivalent (intx of smallest two objectives should be at min buf) */
void test_averaged_ladder() {
    YAML::Node config;
    WebSocketClient client(11, "bola_basic", config);
    BolaBasic bola(client, "bola_basic", config);
   
    const double p = 2.002;

    /* Fake encoded formats with static sizes/SSIMs */
    vector<Encoded> encoded_formats;
    for (unsigned i = 0; i < BolaBasic::NFORMATS; i++) {
        encoded_formats.emplace_back(Encoded { 
                VideoFormat("11x11-11"),
                BolaBasic::size_ladder_bytes.at(i),
                bola.utility(BolaBasic::ssim_index_ladder.at(i)) });
    }
    
    // Re-calculate parameters with test min/max/ladder
    bola.calculate_parameters(encoded_formats);
    bola.fig_1(encoded_formats, p, 0, "test_averaged_ladder");
    bola.fig_2(encoded_formats, p, 0, "test_averaged_ladder");
}

int main() {
    test_paper_ladder();
    test_averaged_ladder();
    return 0;
}
