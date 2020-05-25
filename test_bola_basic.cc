#include <iostream>
#include <array>
#include <cmath>
#include <algorithm>
#include "../bola_basic.hh"
#include "../../media-server/ws_client.hh"

using namespace std;
typedef BolaBasic::Encoded Encoded;
typedef BolaBasic::Parameters Parameters;

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

// Size units don't matter for paper's definition of utility
double utility(unsigned i) {
    return log(sizes.at(i) / sizes.back());
}

int main() {
    // Use min/max buf from Fig 1/2 (not 3p)
    // Use sizes as doubles to exactly match paper's numbers
    double min_buf_chunks = V_static * (sizes[NFORMATS - 2] * (utility(NFORMATS - 1) + gp_static) - 
            sizes[NFORMATS - 1] * (utility(NFORMATS - 2) + gp_static)) / 
                            (sizes[NFORMATS - 2] - sizes[NFORMATS - 1]); 
    cerr << "min_buf_sec " << min_buf_chunks * p << endl;    // should be ~12
    
    double max_buf_chunks = V_static * (utility(0) + gp_static); 
    cerr << "max_buf_sec " << max_buf_chunks * p << endl;    // should be ~22
    
    YAML::Node config;
    WebSocketClient client(11, "bola_basic", config);
    BolaBasic bola(client, "bola_basic", config);
   
    /* For test, use static bitrate ladder from paper */
    vector<Encoded> encoded_formats;
    for (unsigned i = 0; i < NFORMATS; i++) {
        encoded_formats.emplace_back(Encoded { 
                // Use "width" in vf name for plotting
                VideoFormat(to_string(bitrates_Mbps.at(i) * 1000) + "x11-11"), 
                // Size units don't impact BOLA math; just need a size_t
                static_cast<size_t>(sizes.at(i) / 8 * pow(10, 6)), 
                utility(i) });
    }
    
    /* Recover V and gp from bitrate ladder */
    optional<Parameters> params = bola.calculate_parameters(min_buf_chunks, max_buf_chunks, encoded_formats);
    assert(params); 
    cerr << "recovered V = " << params->V << " (static V = " << V_static 
         << "), recovered gp = " << params->gp << " (static gp = " << gp_static << ")" << endl;

    /* Recover figures */
    bola.fig_1(encoded_formats, *params, p, 0);
    bola.fig_2(encoded_formats, *params, p, 0);

    return 0;
}
