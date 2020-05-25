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
}

/* Note BOLA uses the raw value of utility directly. */
double BolaBasic::utility(double raw_ssim) const {
    // returns db in [0, 60] (60 if ssim == 1)
    return ssim_db(raw_ssim);
}

/* Size units affect objective value, but not the decision */
double BolaBasic::objective(Parameters params, const Encoded& encoded, double client_buf_chunks) const {
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
 * Sorts encoded_formats
 */
optional<BolaBasic::Parameters>
BolaBasic::calculate_parameters(double min_buf_chunks, double max_buf_chunks, 
                                vector<Encoded> & encoded_formats) const {
    assert(encoded_formats.size() > 1);
    
    /* Sort encoded formats ascending by size (secondarily, ascending by utility) */
    sort(encoded_formats.begin(), encoded_formats.end(),
         [](const Encoded& a, const Encoded& b) {
                return tie(a.size, a.utility) < tie(b.size, b.utility);
         });
    
    const Encoded & smallest = encoded_formats.front();
    const Encoded & second_smallest = encoded_formats.at(1);
    const Encoded & largest = encoded_formats.back();
    // TODO: handle largest.utility + gp == 0
    size_t size_delta = second_smallest.size - smallest.size; 

    /* 2 ways to divide by zero in gp calculation */
    assert(MIN_BUF_CHUNKS < max_buf_chunks);    

    if (size_delta == 0) {
        // caller handles
        return nullopt;
    }

    // Size units don't affect gp. Utility units do.
    double gp = (
        max_buf_chunks * ( second_smallest.size * smallest.utility - smallest.size * second_smallest.utility ) - 
            /* Best utility = largest (assumes utility is nondecreasing with size, as required by BOLA.) */
            largest.utility * min_buf_chunks * size_delta
     ) / (
        (min_buf_chunks - max_buf_chunks) * size_delta
     );

    double V = max_buf_chunks / (largest.utility + gp);
    assert(V > 0);
    return Parameters{V, gp};
}

VideoFormat BolaBasic::choose_max_objective(const Parameters & params, 
                                            const std::vector<Encoded> & encoded_formats, 
                                            double client_buf_chunks) const {
    const auto chosen = 
        max_element(encoded_formats.begin(), encoded_formats.end(),
                    [this, params, client_buf_chunks](const Encoded& a, const Encoded& b) { 
                          return objective(params, a, client_buf_chunks) < 
                                 objective(params, b, client_buf_chunks);
                    }
                   );
    return chosen->vf;
}

// TODO: Plot Fig 1/2 every x video_ts. Also plot the actual client_buf, bandwidth. 
void BolaBasic::do_logging(uint64_t vts, const Parameters & params) 
{
    log << "vts " << vts << endl;
    
    /* V and gp */
    log << "V = " << params.V << ", gp = " << params.gp << "\n";

    /* Log objectives and decisions (for Fig 1/2) */
    //fig_1(encoded_formats, params.value(), chunk_duration_s, next_vts);
    //fig_2(encoded_formats, params.value(), chunk_duration_s, next_vts);

    // Record mean V/gp/size/ssim.
    // Run confint on .log (compare to BBA)
}

VideoFormat BolaBasic::select_video_format()
{
    const auto & channel = client_.channel();
    double chunk_duration_s = channel->vduration() * 1.0 / channel->timescale();
    double max_buf_chunks = WebSocketClient::MAX_BUFFER_S / chunk_duration_s;
    double client_buf_s = max(client_.video_playback_buf(), 0.0);
    double client_buf_chunks = client_buf_s / chunk_duration_s;
    
    /* 1. Get info for each encoded format */
    vector<Encoded> encoded_formats;
    uint64_t next_vts = client_.next_vts().value();
    const auto & data_map = channel->vdata(next_vts);
    const auto & ssim_map = channel->vssim(next_vts);
    const auto & vformats = channel->vformats();
    
    // BOLA parameter calculations require at least two formats
    if (vformats.size() < 2) {
        assert(not vformats.empty());
        return vformats.front();
    }

    transform(vformats.begin(), vformats.end(), back_inserter(encoded_formats),
          [this, data_map, ssim_map](VideoFormat vf) { 
                return Encoded { vf, get<1>(data_map.at(vf)), ssim_map.at(vf) }; 
          });

    /* 2. Get BOLA parameters */
    optional<Parameters> params = calculate_parameters(MIN_BUF_CHUNKS, max_buf_chunks, encoded_formats);
    if (not params) {
        // Zero size delta => just pick the better SSIM
        // TODO: if delta == 0, does it make sense to return one of the two smallest chunks? 
        // (what if buf is high?)
        // What gets chosen if I throw?
    }
    
    // TODO: test only
    do_logging(next_vts, params.value()); 

    /* 3. Using parameters, calculate objective for each format.
     * Choose format with max objective. */
    return choose_max_objective(params.value(), encoded_formats, client_buf_chunks);
}

/* Output the data for Fig 1. Plotting script can scale objective to match units in paper (size in bits) */
void BolaBasic::fig_1(const vector<Encoded> & encoded_formats, const Parameters & params, 
                      double chunk_duration_s, uint64_t vts) const {
    if (vts > 180180 * 30) return; // TODO: figure out how often it's useful to plot
    
    unsigned nbuf_samples = 3; // Objectives should be linear
    const string outfilename = "fig1_vts" + to_string(vts) + "_out.txt";
    ofstream fig_out{outfilename};
    if (not fig_out.is_open()) throw runtime_error("couldn't open " + outfilename); 
    
    for (const Encoded & encoded : encoded_formats) {
        for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
                fig_out << encoded.vf.width << " "
                        << client_buf_s << " "
                        << objective(params, encoded, client_buf_s / chunk_duration_s) 
                        << "\n";
        }
    }
}

/* Output the data for Fig 2 */
void BolaBasic::fig_2(const vector<Encoded> & encoded_formats, const Parameters & params,
                      double chunk_duration_s, uint64_t vts) const {
    if (vts > 180180 * 30) return; // TODO: figure out how often it's useful to plot
    
    unsigned nbuf_samples = 1000; // Approx stepwise 
    const string outfilename = "fig2_vts" + to_string(vts) + "_out.txt";
    ofstream fig_out{outfilename};
    if (not fig_out.is_open()) throw runtime_error("couldn't open " + outfilename); 
    
    for (double client_buf_s = 0; client_buf_s <= 25.0; client_buf_s += 25.0 / nbuf_samples) {
        fig_out << client_buf_s << " "
                << choose_max_objective(params, encoded_formats, client_buf_s / chunk_duration_s).width
                << "\n";
    }
}
