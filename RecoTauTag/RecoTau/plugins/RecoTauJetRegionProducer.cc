/*
 * RecoTauJetRegionProducer
 *
 * Given a set of PFJets, make new jets with the same p4 but collect all the
 * PFCandidates from a cone of a given size into the constituents.
 *
 * Author: Evan K. Friis, UC Davis
 *
 */

#include <boost/bind.hpp>

#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/Common/interface/Association.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidateFwd.h"
#include "DataFormats/Common/interface/AssociationMap.h"

#include "RecoTauTag/RecoTau/interface/ConeTools.h"
#include "RecoTauTag/RecoTau/interface/RecoTauCommonUtilities.h"

#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

class RecoTauJetRegionProducer : public edm::EDProducer {
  public:
    typedef edm::Association<reco::PFJetCollection> PFJetMatchMap;
    explicit RecoTauJetRegionProducer(const edm::ParameterSet& pset);
    ~RecoTauJetRegionProducer() {}
    void produce(edm::Event& evt, const edm::EventSetup& es) override;
  private:
    float deltaR2_;
    edm::InputTag inputJets_;
    edm::InputTag pfCandSrc_;
    edm::InputTag pfCandAssocMapSrc_;
};

RecoTauJetRegionProducer::RecoTauJetRegionProducer(
    const edm::ParameterSet& pset) {
  deltaR2_ = pset.getParameter<double>("deltaR"); deltaR2_*=deltaR2_;
  inputJets_ = pset.getParameter<edm::InputTag>("src");
  pfCandSrc_ = pset.getParameter<edm::InputTag>("pfCandSrc");
  pfCandAssocMapSrc_ = pset.getParameter<edm::InputTag>("pfCandAssocMapSrc");
  produces<reco::PFJetCollection>("jets");
  produces<PFJetMatchMap>();
}

void RecoTauJetRegionProducer::produce(edm::Event& evt,
    const edm::EventSetup& es) {

  edm::Handle<reco::PFCandidateCollection> pfCandsHandle;
  evt.getByLabel(pfCandSrc_, pfCandsHandle);

  // Build Ptrs for all the PFCandidates
  typedef edm::Ptr<reco::PFCandidate> PFCandPtr;
  std::vector<PFCandPtr> pfCands;
  pfCands.reserve(pfCandsHandle->size());
  for (size_t icand = 0; icand < pfCandsHandle->size(); ++icand) {
    pfCands.push_back(PFCandPtr(pfCandsHandle, icand));
  }

  // Get the jets
  edm::Handle<reco::CandidateView> jetView;
  evt.getByLabel(inputJets_, jetView);
  // Convert to a vector of PFJetRefs
  reco::PFJetRefVector jets =
      reco::tau::castView<reco::PFJetRefVector>(jetView);
  size_t nJets = jets.size();

  // Get the association map matching jets to PFCandidates
  // (needed for recinstruction of boosted taus)
  typedef edm::AssociationMap<edm::OneToMany<std::vector<reco::PFJet>, std::vector<reco::PFCandidate>, unsigned int> > JetToPFCandidateAssociation;
  edm::Handle<JetToPFCandidateAssociation> jetToPFCandMap;
  if ( pfCandAssocMapSrc_.label() != "" ) {
    evt.getByLabel(pfCandAssocMapSrc_, jetToPFCandMap);
  }

  // Get the original product, so we can match against it - otherwise the
  // indices don't match up.
  edm::ProductID originalId = jets.id();
  edm::Handle<reco::PFJetCollection> originalJets;
  size_t nOriginalJets = 0;
  // We have to make sure that we have some selected jets, otherwise we don't
  // actually have a valid product ID to the original jets.
  if (nJets) {
    try {
      evt.get(originalId, originalJets);
    } catch(const cms::Exception &e) {
      edm::LogError("MissingOriginalCollection")
        << "Can't get the original jets that made: " << inputJets_
        << " that have product ID: " << originalId
        << " from the event!!";
      throw e;
    }
    nOriginalJets = originalJets->size();
  }

  std::auto_ptr<reco::PFJetCollection> newJets(new reco::PFJetCollection);

  // Keep track of the indices of the current jet and the old (original) jet
  // -1 indicates no match.
  std::vector<int> matchInfo(nOriginalJets, -1);
  newJets->reserve(nJets);
  for (size_t ijet = 0; ijet < nJets; ++ijet) {
    // Get a ref to jet
    reco::PFJetRef jetRef = jets[ijet];
    // Make an initial copy.
    newJets->emplace_back(*jetRef);
    reco::PFJet& newJet = newJets->back();
    // Clear out all the constituents
    newJet.clearDaughters();
    // Loop over all the PFCands
    for ( std::vector<PFCandPtr>::const_iterator pfCand = pfCands.begin();
	  pfCand != pfCands.end(); ++pfCand ) {
      //      bool isMappedToJet = false;
      if ( pfCandAssocMapSrc_.label() != "" ) {
	edm::RefVector<reco::PFCandidateCollection> pfCandsMappedToJet = (*jetToPFCandMap)[jetRef];
	for ( edm::RefVector<reco::PFCandidateCollection>::const_iterator pfCandMappedToJet = pfCandsMappedToJet.begin();
	      pfCandMappedToJet != pfCandsMappedToJet.end(); ++pfCandMappedToJet ) {
	  if ( reco::deltaR2(**pfCandMappedToJet, **pfCand) < 1.e-8 ) {
	    //    isMappedToJet = true;
	    break;
	  }
	}
      }// else {
      //	isMappedToJet = true;
      //  }
      if ( reco::deltaR2(*jetRef, **pfCand) < deltaR2_ ) newJet.addDaughter(*pfCand);
    }
    // Match the index of the jet we just made to the index into the original
    // collection.
    matchInfo[jetRef.key()] = ijet;
  }

  // Put our new jets into the event
  edm::OrphanHandle<reco::PFJetCollection> newJetsInEvent =
    evt.put(newJets, "jets");

  // Create a matching between original jets -> extra collection
  std::auto_ptr<PFJetMatchMap> matching(new PFJetMatchMap(newJetsInEvent));
  if (nJets) {
    PFJetMatchMap::Filler filler(*matching);
    filler.insert(originalJets, matchInfo.begin(), matchInfo.end());
    filler.fill();
  }
  evt.put(matching);
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(RecoTauJetRegionProducer);
