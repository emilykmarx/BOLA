#include "bola_basic.hh"
#include "ws_client.hh"
#include <math.h>
#include <fstream>
#include <algorithm>

using namespace std;

BolaBasic::BolaBasic(const WebSocketClient & client,
        const string & abr_name, const YAML::Node & abr_config)
    : ABRAlgo(client, abr_name)
{
    // TODO: Do we want anything to be configurable? Utility function, gamma, V, min buf?
    (void)abr_config;
    log.open(log_filename, fstream::out | fstream::trunc);
    if (not log.is_open()) throw runtime_error("couldn't open " + log_filename); 

    calculate_parameters({});
    log << "V = " << params.V << ", gp = " << params.gp << "\n";
}

/* Note BOLA uses the raw value of utility directly. */
double BolaBasic::utility(double raw_ssim) const {
    // returns db in [0, 60] (60 if ssim == 1)
    return ssim_db(raw_ssim);
}

/* Size units affect objective value, but not the decision */
double BolaBasic::objective(const Encoded& encoded, double client_buf_chunks) const {
    return (params.V * (encoded.utility + params.gp) - client_buf_chunks) / encoded.size;
}

/* 
 * Calculate BOLA parameters V and gamma based on min and max buffer size.
 * 1. Min buffer: Intersection of objectives for smallest and next-smallest formats
 *    i.e. set objectives equal, using Q = min buf
 *    TODO: could intersect worst quality and second-worst, but this is closer to our BBA?
 *    This is an extrapolation from the paper.
 * 2. Max buffer: If buffer > max buf, don't send (enforced by media server)
 *    i.e. V(utility_best + gp) = max buf 
 *    This is directly from the paper. 
 * 
 * Note: X_buf_chunks represents a number of chunks, but may be fractional (as in paper) 
 * 
 */
// TODO: encoded_formats is only for test
// TODO: move out of constructor - all static (commit and make graphs first!!!!)
void BolaBasic::calculate_parameters(optional<vector<Encoded>> encoded_formats) {
    // vf is not meaningful, since these are averages over past encodings across channels
    VideoFormat fake("11x11-11");
    // TODO: make const & after test/make a better test
    Encoded smallest = { fake,
        size_ladder_bytes.front(), utility(ssim_index_ladder.front()) };
    Encoded second_smallest = { fake,
        size_ladder_bytes.at(1), utility(ssim_index_ladder.at(1)) };
    Encoded largest = { fake,
        size_ladder_bytes.back(), utility(ssim_index_ladder.back()) };

    if (encoded_formats) {
        smallest = encoded_formats.value().front();
        second_smallest = encoded_formats.value().at(1);
        largest = encoded_formats.value().back();
    }

    size_t size_delta = second_smallest.size - smallest.size; 
    
    // Size units don't affect gp. Utility units do.
    double gp = (
        MAX_BUF_CHUNKS * ( second_smallest.size * smallest.utility - smallest.size * second_smallest.utility ) - 
            /* Best utility = largest (assumes utility is nondecreasing with size, as required by BOLA.) */
            largest.utility * MIN_BUF_CHUNKS * size_delta
     ) / (
        (MIN_BUF_CHUNKS - MAX_BUF_CHUNKS) * size_delta
     );

    double V = MAX_BUF_CHUNKS / (largest.utility + gp);
    params = {V, gp};
}

BolaBasic::Encoded BolaBasic::choose_max_objective(const std::vector<Encoded> & encoded_formats, 
                                            double client_buf_chunks) const {
    const auto chosen = 
        max_element(encoded_formats.begin(), encoded_formats.end(),
                    [this, client_buf_chunks](const Encoded& a, const Encoded& b) { 
                          return objective(a, client_buf_chunks) < 
                                 objective(b, client_buf_chunks);
                    }
                   );
    
    return *chosen;
}

void BolaBasic::do_logging(const std::vector<Encoded> & encoded_formats, 
                           double chunk_duration_s, uint64_t vts, const string & channel_name)
{
    log << channel_name << ", vts " << vts << "\n";

    /* Log objectives and decisions (for Fig 1/2) */
    fig_1(encoded_formats, chunk_duration_s, vts, channel_name);
    fig_2(encoded_formats, chunk_duration_s, vts, channel_name);

    // Could also log mean V/gp/size/ssim, client_buf, bandwidth
    // TODO: Run confint on .log (compare to BBA)
}

VideoFormat BolaBasic::select_video_format()
{
    const auto & channel = client_.channel();
    double chunk_duration_s = channel->vduration() * 1.0 / channel->timescale();
    double client_buf_s = max(client_.video_playback_buf(), 0.0);
    double client_buf_chunks = client_buf_s / chunk_duration_s;
    
    /* 1. Get info for each encoded format */
    vector<Encoded> encoded_formats;
    uint64_t next_vts = client_.next_vts().value();
    const auto & data_map = channel->vdata(next_vts);
    const auto & ssim_map = channel->vssim(next_vts);
    const auto & vformats = channel->vformats();
    
    transform(vformats.begin(), vformats.end(), back_inserter(encoded_formats),
          [this, data_map, ssim_map](const VideoFormat & vf) { 
                return Encoded { vf, get<1>(data_map.at(vf)), utility(ssim_map.at(vf)) }; 
          });

    // TODO: test only
    do_logging(encoded_formats, chunk_duration_s, next_vts, channel->name()); 

    /* 2. Using parameters, calculate objective for each format.
     * Choose format with max objective. */
    return choose_max_objective(encoded_formats, client_buf_chunks).vf;
}

/* Output the data for Fig 1. Plotting script can scale objective to match units in paper (size in bits) */
void BolaBasic::fig_1(const vector<Encoded> & encoded_formats,
                           double chunk_duration_s, uint64_t vts, const string & channel_name) const 
{
    // if (vts > 180180 * NPLOTS) return; 
     
    const string outfilename = "abr/test/" + channel_name + "/fig1_vts" + to_string(vts) + "_out.txt";

    // avoid overwriting 
    ifstream fig_exists{outfilename};
    if (fig_exists) return;
    
    ofstream fig_out{outfilename};
    if (not fig_out.is_open()) throw runtime_error("couldn't open " + outfilename); 
    
    unsigned nbuf_samples = 3; // Objectives are linear
    for (const Encoded & encoded : encoded_formats) {
        for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
            /* Write format, size, utility for Fig 1 */
            fig_out << encoded.vf.to_string() << " "
                    << encoded.size << " "
                    << encoded.utility << " "
                    << client_buf_s << " "
                    << objective(encoded, client_buf_s / chunk_duration_s) 
                    << "\n";
        }
    }
}

/* Output the data for Fig 2 */
void BolaBasic::fig_2(const vector<Encoded> & encoded_formats, 
                           double chunk_duration_s, uint64_t vts, const string & channel_name) const 
{
    // if (vts > 180180 * NPLOTS) return; 
    
    const string outfilename = "abr/test/" + channel_name + "/fig2_vts" + to_string(vts) + "_out.txt";
   
    // avoid overwriting 
    ifstream fig_exists{outfilename};
    if (fig_exists) return;
    
    ofstream fig_out{outfilename};
    if (not fig_out.is_open()) throw runtime_error("couldn't open " + outfilename); 
    
    unsigned nbuf_samples = 1000; // Approx stepwise 
    for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
        const Encoded & chosen = choose_max_objective(encoded_formats, client_buf_s / chunk_duration_s);
        /* Format string and utility already in Fig 1 legend -- just write size */
        fig_out << client_buf_s << " "
                << chosen.size << " "
                << "\n";
    }
}
