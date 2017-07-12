#ifndef __CPU_O3_ILP_PRED_HH__
#define __CPU_O3_ILP_PRED_HH__

#include "cpu/inst_seq.hh"

#include <array>

const int ILPHistoryLength = 64;

class ILPPredictor {

    // instructions issued when IQ head is miss
    int historyIssuedInsts;

    int index;

    std::array<int, ILPHistoryLength> issuedInsts;

    bool isUnderMiss;

    InstSeqNum headInst;


public:

    ILPPredictor();

    void setNewMissHead(InstSeqNum seqNum);

    void incIssued();

    void removeHead(InstSeqNum seqNum);

    double getILP();
};

extern ILPPredictor ILP_predictor[];


#endif // __CPU_O3_ILP_PRED_HH__
