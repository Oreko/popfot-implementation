#include "LdpcDecoder.h"
#include <cassert>
#include "Mtx.h"
#include "LdpcEncoder.h"
#include "LdpcSampler.h"
#include "Util.h"
#include <cmath>
#include <numeric>      // std::iota
#include <algorithm> 
#include <iomanip>

#ifdef ALT_ENCODER
#include "libOTe/Tools/LDPC/alt/LDPC_generator.h"
#include "libOTe/Tools/LDPC/alt/LDPC_decoder.h"
#endif

#include "LdpcImpulseDist.h"
namespace osuCrypto {


    auto nan = std::nan("");

    void LdpcDecoder::init(SparseMtx& H)
    {
        mH = H;
        auto n = mH.cols();
        auto m = mH.rows();

        assert(n > m);
        mK = n - m;

        mR.resize(m, n);
        mM.resize(m, n);

        mW.resize(n);
    }


    std::vector<u8> LdpcDecoder::bpDecode(span<u8> codeword, u64 maxIter)
    {
        auto n = mH.cols();


        assert(codeword.size() == n);

        std::array<double, 2> wVal{ { mP / (1 - mP), (1 - mP) / mP} };
        //std::fill(mR.begin(), mR.end(), nan);
        //std::fill(mM.begin(), mM.end(), nan);

        // #1
        for (u64 i = 0; i < n; ++i)
        {
            assert(codeword[i] < 2);
            mW[i] = wVal[codeword[i]];
        }

        return bpDecode(mW);
    }

    std::vector<u8> LdpcDecoder::bpDecode(span<double> lr, u64 maxIter)
    {


        auto n = mH.cols();
        auto m = mH.rows();

        // #1
        for (u64 i = 0; i < n; ++i)
        {
            //assert(codeword[i] < 2);
            mW[i] = lr[i];

            for (auto j : mH.mCols[i])
            {
                mR(j, i) = mW[i];
            }
        }

        std::vector<u8> c(n);
        std::vector<double> rr; rr.reserve(100);
        for (u64 ii = 0; ii < maxIter; ii++)
        {
            // #2
            for (u64 j = 0; j < m; ++j)
            {
                rr.resize(mH.mRows[j].size());
                for (u64 i : mH.mRows[j])
                {
                    // \Pi_{k in Nj \ {i} }  (r_k^j + 1)/(r_k^j - 1)
                    double v = 1;
                    auto jj = 0;
                    for (u64 k : mH.mRows[j])
                    {
                        rr[jj++] = mR(j, k);
                        if (k != i)
                        {
                            auto r = mR(j, k);
                            v *= (r + 1) / (r - 1);
                        }
                    }

                    // m_j^i 
                    auto mm = (v + 1) / (v - 1);
                    mM(j, i) = mm;
                }
            }

            // i indexes a column, [1,...,n]
            for (u64 i = 0; i < n; ++i)
            {
                // j indexes a row, [1,...,m]
                for (u64 j : mH.mCols[i])
                {
                    // r_i^j = w_i * Pi_{k in Ni \ {j} } m_k^i
                    mR(j, i) = mW[i];

                    // j indexes a row, [1,...,m]
                    for (u64 k : mH.mCols[i])
                    {
                        if (k != j)
                        {
                            mR(j, i) *= mM(k, i);
                        }
                    }
                }
            }

            mL.resize(n);
            // i indexes a column, [1,...,n]
            for (u64 i = 0; i < n; ++i)
            {
                //L(ci | wi, m^i)
                mL[i] = mW[i];

                // k indexes a row, [1,...,m]
                for (u64 k : mH.mCols[i])
                {
                    assert(mM(k, i) != nan);
                    mL[i] *= mM(k, i);
                }

                c[i] = (mL[i] >= 1) ? 0 : 1;


                mL[i] = std::log(mL[i]);
            }

            if (check(c))
            {
                c.resize(n - m);
                return c;
            }
        }

        return {};
    }

    double sgn(double x)
    {
        if (x >= 0)
            return 1;
        return -1;
    }

    u8 sgnBool(double x)
    {
        if (x >= 0)
            return 0;
        return 1;
    }

    double phi(double x)
    {
        assert(x > 0);
        x = std::min(20.0, x);
        auto t = std::tanh(x * 0.5);
        return -std::log(t);
    }

    std::ostream& operator<<(std::ostream& o, const Matrix<double>& m)
    {
        for (u64 i = 0; i < m.rows(); ++i)
        {
            for (u64 j = 0; j < m.cols(); ++j)
            {
                o << std::setw(4) << std::setfill(' ') << m(i, j) << " ";
            }

            o << std::endl;
        }

        return  o;
    }


    std::vector<u8> LdpcDecoder::logbpDecode2(span<u8> codeword, u64 maxIter)
    {

        auto n = mH.cols();
        //auto m = mH.rows();

        assert(codeword.size() == n);

        std::array<double, 3> wVal{
            {std::log(mP / (1 - mP)),
            std::log((1 - mP) / mP)
            }
        };

        std::vector<double> w(n);
        for (u64 i = 0; i < n; ++i)
            w[i] = wVal[codeword[i]];

        return logbpDecode2(w, maxIter);

    }
    std::vector<u8> LdpcDecoder::logbpDecode2(span<double> llr, u64 maxIter)
    {
        auto n = mH.cols();
        auto m = mH.rows();
        std::vector<u8> c(n);
        mL.resize(c.size());


        for (u64 i = 0; i < n; ++i)
        {
            mW[i] = llr[i];

            for (auto j : mH.mCols[i])
            {
                mR(j, i) = mW[i];
            }
        }

        for (u64 ii = 0; ii < maxIter; ii++)
        {
            // #2
            for (u64 j = 0; j < m; ++j)
            {
                double v = 0;
                u8 s = 1;
                for (u64 k : mH.mRows[j])
                {
                    auto rr = mR(j, k);
                    v += phi(abs(rr));
                    s ^= sgnBool(rr);
                }


                for (u64 k : mH.mRows[j])
                {
                    auto vv = phi(abs(mR(j, k)));
                    auto ss = sgnBool(mR(j, k));
                    vv = phi(v - vv);

                    mM(j, k) = (s ^ ss) ? vv : -vv;
                }

                //for (u64 i : mH.mRows[j])
                //{
                //    // \Pi_{k in Nj \ {i} }  (r_k^j + 1)/(r_k^j - 1)
                //    double v = 0;
                //    double s = 1;

                //    for (u64 k : mH.mRows[j])
                //    {
                //        if (k != i)
                //        {
                //            v += phi(abs(mR(j, k)));
                //            s *= sgn(mR(j, k));
                //        }
                //    }

                //    // m_j^i 
                //    auto mm = s * phi(v);
                //    //assert(mM(j, i) == mm);
                //    if (std::abs(mM(j, i) - mm) > 0.001)
                //    {
                //        std::cout << mM(j, i) << " == " << mm << " " << j << " " << i << std::endl;
                //    }
                //}
            }

            // i indexes a column, [1,...,n]
            for (u64 i = 0; i < n; ++i)
            {
                mL[i] = mW[i];
                for (u64 k : mH.mCols[i])
                {
                    mL[i] += mM(k, i);
                }
                for (u64 k : mH.mCols[i])
                {
                    // r_i^j = w_i * Pi_{k in Ni \ {j} } m_k^i
                    mR(k, i) = mL[i] - mM(k, i);
                }

                c[i] = (mL[i] >= 0) ? 0 : 1;

                //// j indexes a row, [1,...,m]
                //for (u64 j : mH.mCols[i])
                //{
                //    // r_i^j = w_i * Pi_{k in Ni \ {j} } m_k^i
                //    auto vv = mW[i];

                //    // j indexes a row, [1,...,m]
                //    for (u64 k : mH.mCols[i])
                //    {
                //        if (k != j)
                //        {
                //            vv += mM(k, i);
                //        }
                //    }

                //    assert(mR(j, i) - vv < 0.00001);
                //}
            }

            //std::vector<double> LL(c.size());
            // i indexes a column, [1,...,n]
            //for (u64 i = 0; i < n; ++i)
            //{
            //    //log L(ci | wi, m^i)
            //    mL[i] = mW[i];

            //    // k indexes a row, [1,...,m]
            //    for (u64 k : mH.mCols[i])
            //    {
            //        assert(mM(k, i) != nan);
            //        mL[i] += mM(k, i);
            //    }

            //    c[i] = (mL[i] >= 0) ? 0 : 1;

            //}

            //if (ii % 1000 == 0)
            //{
            //    for (auto L : LL)
            //        std::cout << L << " ";
            //    std::cout << std::endl;
            //}

            if (check(c))
            {
                if (mAllowZero == false && isZero(c))
                    continue;

                c.resize(n - m);
                return c;
            }
        }

        return {};
    }

    std::vector<u8> LdpcDecoder::altDecode(span<u8> codeword, bool minSum, u64 maxIter)
    {

        //std::vector<uint8_t> LdpcCode::decode(std::vector<double> llr_vec, auto max_iter, bool min_sum) {

        auto _N = mH.cols();
        //auto _M = mH.rows();
        //std::vector<double> llr_vec(_N);
        std::array<double, 3> wVal{
            {std::log(mP / (1 - mP)),
            std::log((1 - mP) / mP)
            }
        };

        std::vector<double> w(_N);
        for (u64 i = 0; i < _N; ++i)
        {
            w[i] = wVal[codeword[i]];
        }
        return altDecode(w, minSum, maxIter);
    }

    std::vector<u8> LdpcDecoder::altDecode(span<double> w, bool min_sum, u64 maxIter)
    {

        auto _N = mH.cols();
        auto _M = mH.rows();

        for (u64 i = 0; i < _N; ++i)
        {
            mW[i] = w[i];
        }

        mL = mW;
        std::vector<uint8_t > decoded_cw(_N);


        std::vector<std::vector<double> > forward_msg(_M);
        std::vector<std::vector<double> > back_msg(_M);
        for (u64 r = 0; r < _M; ++r) {
            forward_msg[r].resize(mH.row(r).size());
            back_msg[r].resize(mH.row(r).size());
        }
        auto maxLL = 20.0;

        for (u64 iter = 0; iter < maxIter; ++iter) {

            for (u64 r = 0; r < _M; ++r) {

                for (u64 c1 = 0; c1 < (u64)mH.row(r).size(); ++c1) {
                    double tmp = 1;
                    if (min_sum)
                        tmp = maxLL;

                    for (u64 c2 = 0; c2 < (u64)mH.row(r).size(); ++c2) {
                        if (c1 == c2)
                            continue;

                        auto i_col2 = mH.row(r)[c2];

                        double l1 = mL[i_col2] - back_msg[r][c2];
                        l1 = std::min(l1, maxLL);
                        l1 = std::max(l1, -maxLL);

                        if (min_sum) {
                            double sign_tmp = tmp < 0 ? -1 : 1;
                            double sign_l1 = l1 < 0.0 ? -1 : 1;

                            tmp = sign_tmp * sign_l1 * std::min(std::abs(l1), std::abs(tmp));
                        }
                        else
                            tmp = tmp * tanh(l1 / 2);
                    }


                    if (min_sum) {
                        forward_msg[r][c1] = tmp;
                    }
                    else {
                        forward_msg[r][c1] = 2 * atanh(tmp);
                    }
                }
            }

            back_msg = forward_msg;

            mL = mW;

            for (u64 r = 0; r < _M; ++r) {

                for (u64 i = 0; i < (u64)mH.row(r).size(); ++i) {
                    auto c = mH.row(r)[i];
                    mL[c] += back_msg[r][i];
                }
            }

            for (u64 c = 0; c < _N; ++c) {
                decoded_cw[c] = mL[c] > 0 ? 0 : 1;
            }

            if (check(decoded_cw)) {
                decoded_cw.resize(_N - _M);
                return decoded_cw;
            }

        } // Iteration loop end


        return {};

        //}

    }

    std::vector<u8> LdpcDecoder::minSumDecode(span<u8> codeword, u64 maxIter)
    {

        auto n = mH.cols();
        auto m = mH.rows();

        assert(codeword.size() == n);

        std::array<double, 2> wVal{
            {std::log(mP / (1 - mP)),
            std::log((1 - mP) / mP)} };

        auto nan = std::nan("");
        std::fill(mR.begin(), mR.end(), nan);
        std::fill(mM.begin(), mM.end(), nan);

        // #1
        for (u64 i = 0; i < n; ++i)
        {
            assert(codeword[i] < 2);
            mW[i] = wVal[codeword[i]];

            for (auto j : mH.mCols[i])
            {
                mR(j, i) = mW[i];
            }
        }

        std::vector<u8> c(n);
        std::vector<double> rr; rr.reserve(100);
        for (u64 ii = 0; ii < maxIter; ii++)
        {
            // #2
            for (u64 j = 0; j < m; ++j)
            {
                rr.resize(mH.mRows[j].size());
                for (u64 i : mH.mRows[j])
                {
                    // \Pi_{k in Nj \ {i} }  (r_k^j + 1)/(r_k^j - 1)
                    double v = std::numeric_limits<double>::max();
                    double s = 1;

                    for (u64 k : mH.mRows[j])
                    {
                        if (k != i)
                        {
                            assert(mR(j, k) != nan);

                            v = std::min(v, std::abs(mR(j, k)));

                            s *= sgn(mR(j, k));
                        }
                    }

                    // m_j^i 
                    mM(j, i) = s * v;
                }
            }

            // i indexes a column, [1,...,n]
            for (u64 i = 0; i < n; ++i)
            {
                // j indexes a row, [1,...,m]
                for (u64 j : mH.mCols[i])
                {
                    // r_i^j = w_i * Pi_{k in Ni \ {j} } m_k^i
                    mR(j, i) = mW[i];

                    // j indexes a row, [1,...,m]
                    for (u64 k : mH.mCols[i])
                    {
                        if (k != j)
                        {
                            //auto mr = mM.rows();
                            //assert(j < mR.rows());
                            //assert(i < mR.cols());
                            //assert(i < mM.rows());
                            //assert(k < mM.cols());
                            assert(mM(k, i) != nan);

                            mR(j, i) += mM(k, i);
                        }
                    }
                }
            }
            mL.resize(n);
            // i indexes a column, [1,...,n]
            for (u64 i = 0; i < n; ++i)
            {
                //log L(ci | wi, m^i)
                mL[i] = mW[i];

                // k indexes a row, [1,...,m]
                for (u64 k : mH.mCols[i])
                {
                    assert(mM(k, i) != nan);
                    mL[i] += mM(k, i);
                }

                c[i] = (mL[i] >= 0) ? 0 : 1;
            }

            if (check(c))
            {
                c.resize(n - m);
                return c;
            }
        }

        return {};
    }

    bool LdpcDecoder::check(const span<u8>& data) {

        //bool isCW = true;
        // j indexes a row, [1,...,m]
        for (u64 j = 0; j < mH.rows(); ++j)
        {
            u8 sum = 0;

            // i indexes a column, [1,...,n]
            for (u64 i : mH.mRows[j])
            {
                sum ^= data[i];
            }

            if (sum)
            {
                return false;
            }
        }
        return true;

        //for (int i = 0; i < mH.rows(); i++) {
        //    auto check_node = check_to_data_id[i];
        //    bool res = false;
        //    for (int j = 0; j < check_degree[i]; j++)
        //        res ^= data[check_node[j]];
        //    if (res)return false;
        //}
        //return true;
    }


    //bool LdpcDecoder::decode2(span<u8> data, u64 iterations)
    //{
    //    auto code_len = mH.cols();
    //    auto msg_len = mK;
    //    //first: initialze
    //    //bool* data_nodes_bin = (bool*)data_nodes;
    //    //bool* messages_bin = (bool*)messages;
    //    std::vector<u8> data_nodes_bin(code_len);
    //    std::vector<u8> messages_bin(msg_len);
    //    auto& data_to_check = mH.mRows;
    //    auto& check_to_data = mH.mCols;

    //    std::vector<u64> data_degree(mH.rows()), check_degree(mH.cols());
    //    for (u64 i = 0; i < data_degree.size(); ++i)
    //        data_degree[i] = mH.mRows[i].size();
    //    for (u64 i = 0; i < check_degree.size(); ++i)
    //        check_degree[i] = mH.mCols[i].size();

    //    for (int i = 0; i < code_len; i++) {
    //        data_nodes_bin[i] = data[i];
    //        auto data_node = data_to_check[i];
    //        for (int j = 0; j < data_degree[i]; j++)
    //            messages_bin[data_node[j]] = data_nodes_bin[i];
    //    }
    //    //second: bp
    //    for (int iter = 0; iter < iterations; iter++) {
    //        for (int i = 0; i < code_len - msg_len; i++) {
    //            bool msg_sum = false;
    //            auto check_node = check_to_data[i];
    //            for (int j = 0; j < check_degree[i]; j++) {
    //                msg_sum ^= messages_bin[check_node[j]];
    //            }
    //            for (int j = 0; j < check_degree[i]; j++) {
    //                messages_bin[check_node[j]] = msg_sum ^ messages_bin[check_node[j]];
    //            }
    //        }
    //        for (int i = 0; i < code_len; i++) {
    //            auto data_node = data_to_check[i];
    //            int pos = 0, neg = 0;
    //            for (int j = 0; j < data_degree[i]; j++) {
    //                if (messages_bin[data_node[j]])pos++;
    //                else neg++;
    //            }

    //            int t = pos - neg + (data_nodes_bin[i] ? 1 : -1);
    //            for (int j = 0; j < data_degree[i]; j++) {
    //                int tt = messages_bin[data_node[j]] ? (t - 1) : (t + 1);
    //                messages_bin[data_node[j]] = (tt == 0) ? data_nodes_bin[i] : (tt > 0);
    //            }
    //            data[i] = t == 0 ? data_nodes_bin[i] : t > 0;
    //        }
    //        //check
    //        if (check(data)) {
    //            return true;
    //        }
    //    }

    //    return false;
    //}

    void tests::LdpcDecode_pb_test(const oc::CLP& cmd)
    {
        u64 rows = cmd.getOr("r", 40);
        u64 cols = static_cast<u64>(rows * cmd.getOr("e", 2.0));
        u64 colWeight = cmd.getOr("cw", 3);
        u64 dWeight = cmd.getOr("dw", 3);
        u64 gap = cmd.getOr("g", 2);

        auto k = cols - rows;

        SparseMtx H;
        LdpcEncoder E;
        LdpcDecoder D;

        for (u64 i = 0; i < 2; ++i)
        {
            oc::PRNG prng(block(i, 1));
            bool b = true;
            u64 tries = 0;
            while (b)
            {
                H = sampleTriangularBand(rows, cols, 
                    colWeight, gap, dWeight, false, prng);
                // H = sampleTriangular(rows, cols, colWeight, gap, prng);
                b = !E.init(H, gap);

                ++tries;
            }

            //std::cout << "samples " << tries << std::endl;

            //u64 d;

            //if (cols < 35)
            //    d = minDist(H.dense(), false).second.size();

#ifdef ALT_ENCODER
            LDPC_bp_decoder DD((int)cols, (int)rows);
            DD.init(H);
#endif
            D.init(H);
            std::vector<u8> m(k), m2, code(cols);

            for (auto& mm : m)
                mm = prng.getBit();

            E.encode(code, m);
            auto ease = 1ull;


            u64 min = 9999999;
            u64 ee = 3;
            while (true)
            {
                auto c = code;
                for (u64 j = 0; j < ease; ++j)
                {
                    c[j] ^= 1;
                }

                u64 e = 0;
                //auto m2 = D.logbpDecode(c);
                //auto m3 = DD.logbpDecode(c);
                m2 = D.logbpDecode2(c);


                if (m2 != m)
                {
                    ++e;
                    min = std::min<u64>(min, ease);
                    //std::cout << "logbpDecode2 failed " << ease << std::endl;
                }
                m2 = D.altDecode(c, false);

                if (m2 != m)
                {
                    ++e;
                    min = std::min<u64>(min, ease);
                    //std::cout << "altDecode 0 failed " << ease << std::endl;
                }


                m2 = D.altDecode(c, true);

                if (m2 != m)
                {
                    min = std::min<u64>(min, ease);
                    ++e;
                    //std::cout << "altDecode 1 failed " << ease << std::endl;
                }
                if (e == ee)
                    break;
                //if (m2 != m && !logBP)
                //    logBP = ease;

                //auto m3 = D.bpDecode(c);
                //if (m3 != m && !BP)
                //    BP = ease;

                //auto m4 = D.minSumDecode(c);
                //if (m4 != m && !minSum)
                //    minSum = ease;

                //if (logBP && BP && minSum)
                //    break;

                ++ease;
            }
            if (ease < 4 || min < 4)
            {
                throw std::runtime_error(LOCATION);
            }

            //std::cout << "high " << ease << std::endl;
        }
        return;

    }



}