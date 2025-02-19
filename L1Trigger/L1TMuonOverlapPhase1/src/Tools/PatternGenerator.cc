/*
 * PatternGenerator.cc
 *
 *  Created on: Nov 8, 2019
 *      Author: kbunkow
 */

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/PatternGenerator.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <boost/range/adaptor/reversed.hpp>

#include "TFile.h"
#include "TDirectory.h"

PatternGenerator::PatternGenerator(const edm::ParameterSet& edmCfg,
                                   const OMTFConfiguration* omtfConfig,
                                   GoldenPatternVec<GoldenPatternWithStat>& gps)
    : PatternOptimizerBase(edmCfg, omtfConfig, gps), eventCntPerGp(gps.size(), 0) {
  edm::LogImportant("l1tOmtfEventPrint") << "constructing PatternGenerator, type: "
                                         << edmCfg.getParameter<string>("patternGenerator") << std::endl;

  if (edmCfg.getParameter<string>("patternGenerator") == "patternGen")
    initPatternGen();
}

PatternGenerator::~PatternGenerator() {}

void PatternGenerator::initPatternGen() {
  //reseting the golden patterns
  unsigned int i = 0;
  for (auto& gp : goldenPatterns) {
    gp->setKeyNumber(i++);  //needed  if patterns were added

    if (gp->key().thePt == 0)
      continue;

    gp->reset();

    int statBinsCnt = 1024;  //gp->getPdf()[0][0].size() * 8; //TODO should be big enough to comprise the pdf tails
    gp->iniStatisitics(statBinsCnt, 1);  //TODO
  }

  edm::LogImportant("l1tOmtfEventPrint") << "PatternGenerator::initPatternGen():" << __LINE__
                                         << " goldenPatterns.size() " << goldenPatterns.size() << std::endl;

  //GoldenPatternResult::setFinalizeFunction(3); TODO why it was this one????
  // edm::LogImportant("l1tOmtfEventPrint") << "reseting golden pattern !!!!!" << std::endl;

  //setting all pdf to 1, this will cause that  when the OmtfProcessor process the input, the result will be based only on the number of fired layers,
  //and then the omtfCand will come from the processor that has the biggest number of fired layers
  //however, if the GoldenPatternResult::finalise3() is used - which just count the number of muonStubs (but do not check if it is valid, i.e. fired the pdf)
  // - the below does not matter
  for (auto& gp : goldenPatterns) {
    for (unsigned int iLayer = 0; iLayer < gp->getPdf().size(); ++iLayer) {
      for (unsigned int iRefLayer = 0; iRefLayer < gp->getPdf()[iLayer].size(); ++iRefLayer) {
        for (unsigned int iBin = 0; iBin < gp->getPdf()[iLayer][iRefLayer].size(); iBin++) {
          gp->pdfAllRef[iLayer][iRefLayer][iBin] = 1;
        }
      }
    }
  }

  //TODO uncomment if filling the ptDeltaPhiHist is needed
  /*  ptDeltaPhiHists.resize(2);
  for(unsigned int iCharge = 0; iCharge <= 1; iCharge++) {
    for(unsigned int iLayer = 0; iLayer < omtfConfig->nLayers(); ++iLayer) { //for the moment filing only ref layer, remove whe
      if(iLayer == 0 || iLayer == 2 || iLayer == 4 || iLayer == 6 || iLayer == 7 || iLayer == 10 || iLayer == 11  || iLayer == 16 || //refLayars
         iLayer == 1 || iLayer == 3 || iLayer == 5  ) //bending layers
      {
        ostringstream name;
        name<<"ptDeltaPhiHist_ch_"<<iCharge<<"_Layer_"<<iLayer;
        int phiFrom = -10;
        int phiTo   = 300; //TODO
        int phiBins = phiTo - phiFrom;

        if(iCharge == 1) {
          phiFrom = -300; //TODO
          phiTo = 10;
        }

        TH2I* ptDeltaPhiHist = new TH2I(name.str().c_str(), name.str().c_str(), 400, 0, 200, phiBins, phiFrom -0.5, phiTo -0.5);
        //cout<<"BinLowEdge "<<ptDeltaPhiHist->GetYaxis()->GetBinLowEdge(100)<<" BinUpEdge "<<ptDeltaPhiHist->GetYaxis()->GetBinUpEdge(100);
        ptDeltaPhiHists[iCharge].push_back(ptDeltaPhiHist);
      }
      else
        ptDeltaPhiHists[iCharge].push_back(nullptr);
    }
  }*/

  /* cannot be called  here, will cause crash
  edm::LogImportant("OMTFReconstruction")<<" PatternGenerator constructor - patterns after modification "<<std::endl;
  for(auto& gp : goldenPatterns) {
    edm::LogImportant("OMTFReconstruction")<<gp->key()<<" "
        <<omtfConfig->getPatternPtRange(gp->key().theNumber).ptFrom
        <<" - "<<omtfConfig->getPatternPtRange(gp->key().theNumber).ptTo<<" GeV"<<std::endl;
  }*/
}

void PatternGenerator::updateStat() {
  //cout<<__FUNCTION__<<":"<<__LINE__<<" omtfCand "<<*omtfCand<<std::endl;;
  AlgoMuon* algoMuon = omtfCand.get();
  if (!algoMuon) {
    edm::LogImportant("l1tOmtfEventPrint") << ":" << __LINE__ << " algoMuon is null" << std::endl;
    throw runtime_error("algoMuon is null");
  }

  double ptSim = simMuon->momentum().pt();
  int chargeSim = (abs(simMuon->type()) == 13) ? simMuon->type() / -13 : 0;

  unsigned int exptPatNum = omtfConfig->getPatternNum(ptSim, chargeSim);
  GoldenPatternWithStat* exptCandGp = goldenPatterns.at(exptPatNum).get();  // expected pattern

  eventCntPerGp[exptPatNum]++;

  //edm::LogImportant("l1tOmtfEventPrint")<<"\n" <<__FUNCTION__<<": "<<__LINE__<<" exptCandGp "<<exptCandGp->key()<<" candProcIndx "<<candProcIndx<<" ptSim "<<ptSim<<" chargeSim "<<chargeSim<<std::endl;

  int pdfMiddle = 1 << (omtfConfig->nPdfAddrBits() - 1);

  //iRefHit is the index of the hit
  for (unsigned int iRefHit = 0; iRefHit < exptCandGp->getResults()[candProcIndx].size(); ++iRefHit) {
    auto& gpResult = exptCandGp->getResults()[candProcIndx][iRefHit];

    unsigned int refLayer = gpResult.getRefLayer();

    if (gpResult.getFiredLayerCnt() >= 3) {
      for (unsigned int iLayer = 0; iLayer < gpResult.getStubResults().size(); iLayer++) {
        //updating statistic for the gp which should have fired

        bool fired = false;
        if (gpResult.getStubResults()[iLayer].getMuonStub()) {
          if (omtfConfig->isBendingLayer(iLayer)) {
            if (gpResult.getStubResults()[iLayer].getMuonStub()->qualityHw >= 4)  //TODO change quality cut if needed
              fired = true;
          } else
            fired = true;
        }

        if (fired) {  //the result is not empty
          int phiDist = gpResult.getStubResults()[iLayer].getPdfBin();
          phiDist += exptCandGp->meanDistPhiValue(iLayer, refLayer) - pdfMiddle;
          //removing the shift applied in the GoldenPatternBase::process1Layer1RefLayer

          //TODO uncomment if filling ptDeltaPhiHists is needed
          /*
          unsigned int refLayerLogicNum = omtfConfig->getRefToLogicNumber()[iRefHit];
          if(ptDeltaPhiHists[iCharge][iLayer] != nullptr &&
              (iLayer == refLayerLogicNum || omtfConfig->getLogicToLogic().at(iLayer) == (int)refLayerLogicNum) )
            ptDeltaPhiHists[iCharge][iLayer]->Fill(ttAlgoMuon->getPt(), phiDist); //TODO correct
           */

          phiDist += exptCandGp->getStatistics()[iLayer][refLayer].size() / 2;

          //edm::LogImportant("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<__LINE__<<" refLayer "<<refLayer<<" iLayer "<<iLayer<<" phiDist "<<phiDist<<" getPdfBin "<<gpResult.getStubResults()[iLayer].getPdfBin()<<std::endl;
          if (phiDist > 0 && phiDist < (int)(exptCandGp->getStatistics()[iLayer][refLayer].size())) {
            //updating statistic for the gp which found the candidate
            //edm::LogImportant("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<__LINE__<<" updating statistic "<<std::endl;
            exptCandGp->updateStat(iLayer, refLayer, phiDist, 0, 1);
          }
        } else {  //if there is no hit at all in a given layer, the bin = 0 is filled
          int phiDist = 0;
          exptCandGp->updateStat(iLayer, refLayer, phiDist, 0, 1);
        }
      }
    }
  }
}

void PatternGenerator::observeEventEnd(const edm::Event& iEvent,
                                       std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) {
  if (simMuon == nullptr || omtfCand->getGoldenPatern() == nullptr)  //no sim muon or empty candidate
    return;

  if (abs(simMuon->momentum().eta()) < 0.8 || abs(simMuon->momentum().eta()) > 1.24)
    return;

  PatternOptimizerBase::observeEventEnd(iEvent, finalCandidates);

  updateStat();
}

void PatternGenerator::endJob() {
  if (edmCfg.getParameter<string>("patternGenerator") == "modifyClassProb")
    modifyClassProb(1);
  else if (edmCfg.getParameter<string>("patternGenerator") == "groupPatterns")
    groupPatterns();
  else if (edmCfg.getParameter<string>("patternGenerator") == "patternGen") {
    upadatePdfs();
    writeLayerStat = true;
  } else if (edmCfg.getParameter<string>("patternGenerator") == "patternGenFromStat") {
    std::string rootFileName = edmCfg.getParameter<edm::FileInPath>("patternsROOTFile").fullPath();
    edm::LogImportant("l1tOmtfEventPrint") << "PatternGenerator::endJob() rootFileName " << rootFileName << std::endl;
    TFile inFile(rootFileName.c_str());
    TDirectory* curDir = (TDirectory*)inFile.Get("layerStats");

    ostringstream ostrName;
    for (auto& gp : goldenPatterns) {
      if (gp->key().thePt == 0)
        continue;

      int statBinsCnt = 1024;  //= gp->getPdf()[0][0].size() * 8; //TODO should be big enough to comprise the pdf tails
      gp->iniStatisitics(statBinsCnt, 1);  //TODO

      for (unsigned int iLayer = 0; iLayer < gp->getPdf().size(); ++iLayer) {
        for (unsigned int iRefLayer = 0; iRefLayer < gp->getPdf()[iLayer].size(); ++iRefLayer) {
          ostrName.str("");
          ostrName << "histLayerStat_PatNum_" << gp->key().theNumber << "_refLayer_" << iRefLayer << "_Layer_"
                   << iLayer;

          TH1I* histLayerStat = (TH1I*)curDir->Get(ostrName.str().c_str());

          if (histLayerStat) {
            for (int iBin = 0; iBin < statBinsCnt; iBin++) {
              gp->updateStat(iLayer, iRefLayer, iBin, 0, histLayerStat->GetBinContent(iBin + 1));
            }
          } else {
            edm::LogImportant("l1tOmtfEventPrint")
                << "PatternGenerator::endJob() - reading histLayerStat: histogram not found " << ostrName.str()
                << std::endl;
          }
        }
      }
    }

    TH1* simMuFoundByOmtfPt_fromFile = (TH1*)inFile.Get("simMuFoundByOmtfPt");
    for (unsigned int iGp = 0; iGp < eventCntPerGp.size(); iGp++) {
      eventCntPerGp[iGp] = simMuFoundByOmtfPt_fromFile->GetBinContent(simMuFoundByOmtfPt_fromFile->FindBin(iGp));
      edm::LogImportant("l1tOmtfEventPrint")
          << "PatternGenerator::endJob() - eventCntPerGp: iGp" << iGp << " - " << eventCntPerGp[iGp] << std::endl;
    }

    //TODO chose the desired grouping ///////////////
    int group = 0;
    int indexInGroup = 0;
    for (auto& gp : goldenPatterns) {
      indexInGroup++;
      gp->key().setGroup(group);
      gp->key().setIndexInGroup(indexInGroup);
      //indexInGroup is counted from 1

      edm::LogImportant("l1tOmtfEventPrint")
          << "setGroup(group): group " << group << " indexInGroup " << indexInGroup << std::endl;

      if (gp->key().thePt <= 10 && indexInGroup == 2) {  //TODO
        indexInGroup = 0;
        group++;
      }

      if (gp->key().thePt > 10 && indexInGroup == 4) {  //TODO
        indexInGroup = 0;
        group++;
      }
    }  /////////////////////////////////////////////

    upadatePdfs();

    modifyClassProb(1);

    //groupPatterns(); IMPORTANT don't call grouping here, just set the groups above!!!!

    reCalibratePt();
    this->writeLayerStat = true;
  }

  PatternOptimizerBase::endJob();
}

void PatternGenerator::upadatePdfs() {
  //TODO setting the DistPhiBitShift i.e. grouping of the pdfBins
  for (auto& gp : goldenPatterns) {
    if (gp->key().thePt == 0)
      continue;
    for (unsigned int iLayer = 0; iLayer < gp->getPdf().size(); ++iLayer) {
      for (unsigned int iRefLayer = 0; iRefLayer < gp->getPdf()[iLayer].size(); ++iRefLayer) {
        if (gp->getDistPhiBitShift(iLayer, iRefLayer)) {
          throw runtime_error(
              string(__FUNCTION__) + ":" + to_string(__LINE__) +
              "gp->getDistPhiBitShift(iLayer, iRefLayer) != 0 -  cannot change DistPhiBitShift then!!!!");
        }

        if ((gp->key().thePt <= 10) && (iLayer == 1 || iLayer == 3 || iLayer == 5)) {
          gp->setDistPhiBitShift(1, iLayer, iRefLayer);
        } else
          gp->setDistPhiBitShift(0, iLayer, iRefLayer);

        //watch out: the shift in a given layer must be the same for patterns in one group
        //todo  make the setting of the shift on the group base
        //TODO set the DistPhiBitShift
        /*if( (gp->key().thePt <= 10) && (iLayer == 3 || iLayer == 5 ) && (iRefLayer == 0 || iRefLayer == 2 || iRefLayer == 6 || iRefLayer == 7)) {
          gp->setDistPhiBitShift(3, iLayer, iRefLayer);
        }
        else if( (gp->key().thePt <= 10) && ( iLayer == 1 || iLayer == 3 || iLayer == 5 ) ) {
          gp->setDistPhiBitShift(2, iLayer, iRefLayer);
        }
        else if( ( (gp->key().thePt <= 10) && (iLayer == 7 ||iLayer == 8 || iLayer == 17 ) ) ) {
          gp->setDistPhiBitShift(1, iLayer, iRefLayer);
        }
        else if( (gp->key().thePt <= 10) && (iLayer == 10 || iLayer == 11 || iLayer == 12 || iLayer == 13) && (iRefLayer == 1)) {
          gp->setDistPhiBitShift(1, iLayer, iRefLayer);
        }*/
      }
    }
  }

  double minHitCntThresh = 0.001;
  //Calculating meanDistPhi
  for (auto& gp : goldenPatterns) {
    if (gp->key().thePt == 0)
      continue;

    int minHitCnt = minHitCntThresh * eventCntPerGp[gp->key().number()];  // //TODO tune threshold <<<<<<<<<<<<<<<<<<
    edm::LogImportant("l1tOmtfEventPrint")
        << "PatternGenerator::upadatePdfs() Calculating meanDistPhi " << gp->key() << " eventCnt "
        << eventCntPerGp[gp->key().number()] << " minHitCnt " << minHitCnt << std::endl;

    for (unsigned int iLayer = 0; iLayer < gp->getPdf().size(); ++iLayer) {
      for (unsigned int iRefLayer = 0; iRefLayer < gp->getPdf()[iLayer].size(); ++iRefLayer) {
        //calculate meanDistPhi
        double meanDistPhi = 0;
        double count = 0;
        for (unsigned int iBin = 1; iBin < gp->getStatistics()[iLayer][iRefLayer].size(); iBin++) {
          //iBin = 0 is reserved for the no hit
          meanDistPhi += iBin * gp->getStatistics()[iLayer][iRefLayer][iBin][0];
          count += gp->getStatistics()[iLayer][iRefLayer][iBin][0];
        }

        if (count != 0) {
          meanDistPhi /= count;

          meanDistPhi -= (gp->getStatistics()[iLayer][iRefLayer].size() / 2);

          if (count < minHitCnt)
            meanDistPhi = 0;
          else
            edm::LogImportant("l1tOmtfEventPrint")
                << __FUNCTION__ << ": " << __LINE__ << " " << gp->key() << " iLayer " << iLayer << " iRefLayer "
                << iRefLayer << " count " << count << " meanDistPhi " << meanDistPhi << endl;
        }
        gp->setMeanDistPhiValue(round(meanDistPhi), iLayer, iRefLayer);
      }
    }
  }

  OMTFConfiguration::vector2D patternGroups = omtfConfig->getPatternGroups(goldenPatterns);
  edm::LogImportant("l1tOmtfEventPrint") << "patternGroups:" << std::endl;
  for (unsigned int iGroup = 0; iGroup < patternGroups.size(); iGroup++) {
    edm::LogImportant("l1tOmtfEventPrint") << "patternGroup " << std::setw(2) << iGroup << " ";
    for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
      edm::LogImportant("l1tOmtfEventPrint") << i << " patNum " << patternGroups[iGroup][i] << " ";
    }
    edm::LogImportant("l1tOmtfEventPrint") << std::endl;
  }

  //averaging the meanDistPhi for the gp belonging to the same group
  for (unsigned int iLayer = 0; iLayer < goldenPatterns.at(0)->getPdf().size(); ++iLayer) {
    for (unsigned int iRefLayer = 0; iRefLayer < goldenPatterns.at(0)->getPdf()[iLayer].size(); ++iRefLayer) {
      //unsigned int refLayerLogicNum = omtfConfig->getRefToLogicNumber()[iRefLayer];
      //if(refLayerLogicNum == iLayer)
      {
        for (unsigned int iGroup = 0; iGroup < patternGroups.size(); iGroup++) {
          double meanDistPhi = 0;
          int mergedCnt = 0;
          for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
            auto gp = goldenPatterns.at(patternGroups[iGroup][i]).get();
            meanDistPhi += gp->meanDistPhiValue(iLayer, iRefLayer);
            if (gp->meanDistPhiValue(iLayer, iRefLayer) != 0)
              mergedCnt++;
          }

          if (mergedCnt) {
            meanDistPhi /= mergedCnt;
            //because for some gps the statistics can be too low, and then the meanDistPhiValue is 0, so it should not contribute
            for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
              auto gp = goldenPatterns.at(patternGroups[iGroup][i]).get();
              gp->setMeanDistPhiValue(round(meanDistPhi), iLayer, iRefLayer);
              edm::LogImportant("l1tOmtfEventPrint")
                  << __FUNCTION__ << ": " << __LINE__ << " iGroup " << iGroup << " numInGroup " << i << " " << gp->key()
                  << " iLayer " << iLayer << " iRefLayer " << iRefLayer << " meanDistPhi after averaging "
                  << meanDistPhi << endl;
            }
          }
        }
      }
    }
  }

  //calculating the pdfs
  for (auto& gp : goldenPatterns) {
    if (gp->key().thePt == 0)
      continue;

    //TODO tune threshold <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    int minHitCnt = 2 * minHitCntThresh * eventCntPerGp[gp->key().number()];

    for (unsigned int iLayer = 0; iLayer < gp->getPdf().size(); ++iLayer) {
      for (unsigned int iRefLayer = 0; iRefLayer < gp->getPdf()[iLayer].size(); ++iRefLayer) {
        {
          double norm = 0;
          for (unsigned int iBin = 0; iBin < gp->getStatistics()[iLayer][iRefLayer].size();
               iBin++) {  //iBin = 0 i.e. no hit is included here, to have the proper norm
            norm += gp->getStatistics()[iLayer][iRefLayer][iBin][0];
          }

          int pdfMiddle = gp->getPdf()[iLayer][iRefLayer].size() / 2;
          int statBinGroupSize = 1 << gp->getDistPhiBitShift(iLayer, iRefLayer);
          for (unsigned int iBinPdf = 0; iBinPdf < gp->getPdf()[iLayer][iRefLayer].size(); iBinPdf++) {
            double pdfVal = 0;
            if (iBinPdf > 0) {
              int groupedBins = 0;
              for (int i = 0; i < statBinGroupSize; i++) {
                int iBinStat =
                    statBinGroupSize * ((int)(iBinPdf)-pdfMiddle) + i + gp->meanDistPhiValue(iLayer, iRefLayer);

                iBinStat += (gp->getStatistics()[iLayer][iRefLayer].size() / 2);

                if (iBinStat >= 0 && iBinStat < (int)gp->getStatistics()[iLayer][iRefLayer].size()) {
                  pdfVal += gp->getStatistics()[iLayer][iRefLayer][iBinStat][0];
                  groupedBins++;
                  //cout<<__FUNCTION__<<": "<<__LINE__<<" "<<gp->key()<<" iLayer "<<iLayer<<" iBinStat "<<iBinStat<<" iBinPdf "<<iBinPdf<<" statVal "<<gp->getStatistics()[iLayer][iRefLayer][iBinStat][0]<<endl;
                }
              }
              if (norm > minHitCnt) {
                pdfVal /= (norm * statBinGroupSize);
              } else
                pdfVal = 0;
              /*edm::LogImportant("l1tOmtfEventPrint")
                                << __FUNCTION__ << ": " << __LINE__ << " " << gp->key() << "calculating pdf: iLayer " << iLayer
                                << " iRefLayer " << iRefLayer //<< " norm " << std::setw(5) << norm
                                << " pdfVal " << pdfVal << endl;*/
            } else {  //iBinPdf == 0 i.e. no hit
              int iBinStat = 0;
              if (norm > 0) {
                pdfVal = gp->getStatistics()[iLayer][iRefLayer][iBinStat][0] / norm;
              }
              edm::LogImportant("l1tOmtfEventPrint")
                  << __FUNCTION__ << ": " << __LINE__ << " " << gp->key() << "calculating pdf: iLayer " << iLayer
                  << " iRefLayer " << iRefLayer << " norm " << std::setw(5) << norm << " no hits cnt " << std::setw(5)
                  << gp->getStatistics()[iLayer][iRefLayer][iBinStat][0] << " pdfVal " << pdfVal << endl;
            }

            double minPdfValFactor = 1;
            const double minPlog = log(omtfConfig->minPdfVal() * minPdfValFactor);
            const double pdfMaxVal = omtfConfig->pdfMaxValue();

            int digitisedVal = 0;
            if (pdfVal >= omtfConfig->minPdfVal() * minPdfValFactor) {
              digitisedVal = rint(pdfMaxVal - log(pdfVal) / minPlog * pdfMaxVal);
            }

            gp->setPdfValue(digitisedVal, iLayer, iRefLayer, iBinPdf);
            //cout<<__FUNCTION__<<": "<<__LINE__<<" "<<gp->key()<<" iLayer "<<iLayer<<" iBinPdf "<<iBinPdf<<" pdfVal "<<pdfVal<<" digitisedVal "<<digitisedVal<<endl;
          }
        }
      }
    }
  }
}

void PatternGenerator::saveHists(TFile& outfile) {
  outfile.mkdir("ptDeltaPhiHists")->cd();
  //TODO uncomment if ptDeltaPhiHists are needed
  /*  for(unsigned int iCharge = 0; iCharge <= 1; iCharge++) {
    for(unsigned int iLayer = 0; iLayer < omtfConfig->nLayers(); ++iLayer) { //for the moment filing only ref layer, remove whe
      if(ptDeltaPhiHists[iCharge][iLayer]) {
        ptDeltaPhiHists[iCharge][iLayer]->Write();
      }
    }
  }*/
}

void PatternGenerator::modifyClassProb(double step) {
  edm::LogImportant("l1tOmtfEventPrint") << __FUNCTION__ << ": " << __LINE__ << " Correcting P(C_k) " << std::endl;
  unsigned int iPdf = omtfConfig->nPdfBins() / 2;  // <<(omtfConfig->nPdfAddrBits()-1);
  for (unsigned int iRefLayer = 0; iRefLayer < goldenPatterns[0]->getPdf()[0].size(); ++iRefLayer) {
    unsigned int refLayerLogicNumber = omtfConfig->getRefToLogicNumber()[iRefLayer];
    if (iRefLayer == 0 || iRefLayer == 2)  //DT
      step = 1.5;
    else if (iRefLayer == 5)  //DT
      step = 1.5;
    else if (iRefLayer == 1)  //CSC
      step = 1.5;
    else if (iRefLayer == 3)  //CSC
      step = 1.5;
    else if (iRefLayer == 5)  //RE2/3
      step = 1.5;
    else if (iRefLayer == 6 || iRefLayer == 7)  //bRPC
      step = 1.5;

    edm::LogImportant("l1tOmtfEventPrint")
        << __FUNCTION__ << ":" << __LINE__ << " RefLayer " << iRefLayer << " step " << step << std::endl;
    for (int sign = -1; sign <= 1; sign++) {
      for (auto& gp : boost::adaptors::reverse(goldenPatterns)) {
        if (gp->key().thePt == 0 || gp->key().theCharge != sign)
          continue;

        double ptFrom = omtfConfig->getPatternPtRange(gp->key().theNumber).ptFrom;
        double ptTo = omtfConfig->getPatternPtRange(gp->key().theNumber).ptTo;

        double ptRange = ptTo - ptFrom;

        double minPdfValFactor = 0.1;
        double minPlog = log(omtfConfig->minPdfVal() * minPdfValFactor);
        double pdfMaxVal = omtfConfig->pdfMaxValue();

        pdfMaxVal /= 3.;
        minPlog *= 2;

        //last bin of the ptRange goes to 10000, so here we change it to 1000
        if (ptRange > 800)
          ptRange = 800;

        double norm = 0.001;
        double classProb = vxIntegMuRate(ptFrom, ptRange, 0.82, 1.24) * norm;

        int digitisedVal = rint(pdfMaxVal - log(classProb) / minPlog * pdfMaxVal);

        int newPdfVal = digitisedVal;  //gp->getPdf()[refLayerLogicNumber][iRefLayer][iPdf]

        if (ptFrom == 0)
          newPdfVal += 15;
        if (ptFrom == 3.5)
          newPdfVal += 15;
        if (ptFrom == 4)
          newPdfVal += 12;
        if (ptFrom == 4.5)
          newPdfVal += 9;
        if (ptFrom == 5)
          newPdfVal += 7;
        if (ptFrom == 6)
          newPdfVal += 4;
        if (ptFrom == 7)
          newPdfVal += 2;

        if (ptFrom == 100)
          newPdfVal = 16;
        if (ptFrom == 200)
          newPdfVal = 22;

        gp->setPdfValue(newPdfVal, refLayerLogicNumber, iRefLayer, iPdf);

        edm::LogImportant("l1tOmtfEventPrint")
            << gp->key() << " " << omtfConfig->getPatternPtRange(gp->key().theNumber).ptFrom << " - "
            << omtfConfig->getPatternPtRange(gp->key().theNumber).ptTo << " GeV"
            << " ptRange " << ptRange << " RefLayer " << iRefLayer << " newPdfVal " << newPdfVal << std::endl;
      }
    }
  }
}

void PatternGenerator::reCalibratePt() {
  edm::LogImportant("l1tOmtfEventPrint") << __FUNCTION__ << ": " << __LINE__ << " reCalibratePt" << std::endl;
  std::map<int, float> ptMap;
  //for Patterns_0x0009_oldSample_3_10Files_classProb2.xml
  ptMap[7] = 4.0;
  ptMap[8] = 4.5;
  ptMap[9] = 5.0;
  ptMap[10] = 5.5;
  ptMap[11] = 6.0;
  ptMap[13] = 7.0;
  ptMap[15] = 8.5;
  ptMap[17] = 10.0;
  ptMap[21] = 12.0;
  ptMap[25] = 14.0;
  ptMap[29] = 16.0;
  ptMap[33] = 18.5;
  ptMap[37] = 21.0;
  ptMap[41] = 23.0;
  ptMap[45] = 26.0;
  ptMap[49] = 28.0;
  ptMap[53] = 30.0;
  ptMap[57] = 32.0;
  ptMap[61] = 36.0;
  ptMap[71] = 40.0;
  ptMap[81] = 48.0;
  ptMap[91] = 54.0;
  ptMap[101] = 60.0;
  ptMap[121] = 70.0;
  ptMap[141] = 82.0;
  ptMap[161] = 96.0;
  ptMap[201] = 114.0;
  ptMap[401] = 200.0;

  for (auto& gp : goldenPatterns) {
    if (gp->key().thePt == 0)
      continue;

    int newPt = omtfConfig->ptGevToHw(ptMap[gp->key().thePt]);
    edm::LogImportant("l1tOmtfEventPrint") << gp->key().thePt << " -> " << newPt << std::endl;

    gp->key().setPt(newPt);
  }
}

void PatternGenerator::groupPatterns() {
  int group = 0;
  int indexInGroup = 0;
  for (auto& gp : goldenPatterns) {
    indexInGroup++;
    gp->key().setGroup(group);
    gp->key().setIndexInGroup(indexInGroup);
    //indexInGroup is counted from 1

    edm::LogImportant("l1tOmtfEventPrint")
        << "setGroup(group): group " << group << " indexInGroup " << indexInGroup << std::endl;

    if (gp->key().thePt <= 12 && indexInGroup == 2) {  //TODO
      indexInGroup = 0;
      group++;
    }

    if (gp->key().thePt > 12 && indexInGroup == 4) {  //TODO
      indexInGroup = 0;
      group++;
    }
  }

  OMTFConfiguration::vector2D patternGroups = omtfConfig->getPatternGroups(goldenPatterns);
  edm::LogImportant("l1tOmtfEventPrint") << "patternGroups:" << std::endl;
  for (unsigned int iGroup = 0; iGroup < patternGroups.size(); iGroup++) {
    edm::LogImportant("l1tOmtfEventPrint") << "patternGroup " << std::setw(2) << iGroup << " ";
    for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
      edm::LogImportant("l1tOmtfEventPrint") << i << " patNum " << patternGroups[iGroup][i] << " ";
    }
    edm::LogImportant("l1tOmtfEventPrint") << std::endl;
  }

  int pdfBins = exp2(omtfConfig->nPdfAddrBits());

  for (unsigned int iLayer = 0; iLayer < goldenPatterns.at(0)->getPdf().size(); ++iLayer) {
    for (unsigned int iRefLayer = 0; iRefLayer < goldenPatterns.at(0)->getPdf()[iLayer].size(); ++iRefLayer) {
      //unsigned int refLayerLogicNum = omtfConfig->getRefToLogicNumber()[iRefLayer];
      //if(refLayerLogicNum == iLayer)
      {
        //averaging the meanDistPhi for the gp belonging to the same group
        for (unsigned int iGroup = 0; iGroup < patternGroups.size(); iGroup++) {
          double meanDistPhi = 0;
          int mergedCnt = 0;
          for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
            auto gp = goldenPatterns.at(patternGroups[iGroup][i]).get();
            meanDistPhi += gp->meanDistPhiValue(iLayer, iRefLayer);
            if (gp->meanDistPhiValue(iLayer, iRefLayer) != 0)
              mergedCnt++;
            edm::LogImportant("l1tOmtfEventPrint")
                << __FUNCTION__ << ": " << __LINE__ << " iGroup " << iGroup << " numInGroup " << i << " " << gp->key()
                << " iLayer " << iLayer << " iRefLayer " << iRefLayer << " old meanDistPhiValue "
                << gp->meanDistPhiValue(iLayer, iRefLayer) << endl;
          }

          if (mergedCnt) {
            meanDistPhi /= mergedCnt;
            meanDistPhi = (int)meanDistPhi;

            //because for some gps the statistics can be too low, and then the meanDistPhiValue is 0, so it should not contribute
            for (unsigned int i = 0; i < patternGroups[iGroup].size(); i++) {
              auto gp = goldenPatterns.at(patternGroups[iGroup][i]).get();
              unsigned int refLayerLogicNum = omtfConfig->getRefToLogicNumber()[iRefLayer];
              if (refLayerLogicNum != iLayer) {
                int shift = meanDistPhi - gp->meanDistPhiValue(iLayer, iRefLayer);
                edm::LogImportant("l1tOmtfEventPrint")
                    << __FUNCTION__ << ": " << __LINE__ << " iGroup " << iGroup << " numInGroup " << i << " "
                    << gp->key() << " iLayer " << iLayer << " iRefLayer " << iRefLayer
                    << " new meanDistPhi after averaging " << meanDistPhi << " old meanDistPhiValue "
                    << gp->meanDistPhiValue(iLayer, iRefLayer) << " shift " << shift << endl;

                if (shift < 0) {
                  for (int iBin = 1 - shift; iBin < pdfBins;
                       iBin++) {  //iBin = 0 i.e. no hit is included here, to have the proper norm
                    auto pdfVal = gp->pdfValue(iLayer, iRefLayer, iBin);
                    gp->setPdfValue(pdfVal, iLayer, iRefLayer, iBin + shift);
                    edm::LogImportant("l1tOmtfEventPrint")
                        << "     iBin " << iBin << "  iBin + shift " << iBin + shift << " pdfVal " << pdfVal << endl;
                  }
                  for (int iBin = pdfBins + shift; iBin < pdfBins; iBin++) {
                    gp->setPdfValue(0, iLayer, iRefLayer, iBin);
                  }
                } else if (shift > 0) {
                  for (int iBin = pdfBins - 1 - shift; iBin > 0;
                       iBin--) {  //iBin = 0 i.e. no hit is included here, to have the proper norm
                    auto pdfVal = gp->pdfValue(iLayer, iRefLayer, iBin);
                    gp->setPdfValue(pdfVal, iLayer, iRefLayer, iBin + shift);
                    edm::LogImportant("l1tOmtfEventPrint")
                        << "     iBin " << iBin << "  iBin + shift " << iBin + shift << " pdfVal " << pdfVal << endl;
                  }
                  for (int iBin = shift; iBin > 0; iBin--) {
                    gp->setPdfValue(0, iLayer, iRefLayer, iBin);
                  }
                }
              }

              gp->setMeanDistPhiValue(round(meanDistPhi), iLayer, iRefLayer);
            }
          }
        }
      }
    }
  }
}
