#include "CaloCellCollection.h"
#include "CaloCluster/CaloClusterContainer.h"
#include "CaloCell/RawCell.h"
#include "EventInfo/EventInfoContainer.h"
#include "G4Kernel/constants.h"
#include "CaloCellMaker.h"
#include "TVector3.h"
#include <cstdlib>
#include "G4SystemOfUnits.hh"


using namespace Gaugi;
using namespace SG;
using namespace CaloSampling;




CaloCellMaker::CaloCellMaker( std::string name ) : 
  IMsgService(name),
  Algorithm(),
  m_bcid_truth( special_bcid_for_truth_reconstruction )
{

  declareProperty( "EventKey"                 , m_eventKey="EventInfo"                );
  declareProperty( "HistogramPath"            , m_histPath="/CaloCellMaker"           );
  declareProperty( "CaloCellFile"             , m_caloCellFile                        );
  declareProperty( "CollectionKey"            , m_collectionKey="CaloCellCollection"  );
  declareProperty( "BunchIdStart"             , m_bcid_start=-7                       );
  declareProperty( "BunchIdEnd"               , m_bcid_end=8                          );
  declareProperty( "BunchDuration"            , m_bc_duration=25                      );
  declareProperty( "NumberOfSamplesPerBunch"  , m_bc_nsamples=1                       );
  declareProperty( "OutputLevel"              , m_outputLevel=1                       );
  declareProperty( "DetailedHistograms"       , m_detailedHistograms=false            );
}


void CaloCellMaker::push_back( Gaugi::AlgTool* tool )
{
  m_toolHandles.push_back(tool);
}


StatusCode CaloCellMaker::initialize()
{
  // Set message level
  setMsgLevel( (MSG::Level)m_outputLevel );
  
  // Read the file
  std::ifstream file(m_caloCellFile);

	std::string line;
	while (std::getline(file, line))
	{
    std::string command;
    // Get the command
    file >> command;
    // Layer configuration
    if (command=="L"){
      file >> m_sampling >> m_eta_min >> m_eta_max >> m_eta_bins >> m_phi_min >> m_phi_max >> m_phi_bins >> m_rmin >> m_rmax;
      break;
    }
	}
  file.close();

  for ( auto tool : m_toolHandles )
  {
    if (tool->initialize().isFailure() )
    {
      MSG_FATAL( "It's not possible to iniatialize " << tool->name() << " tool." );
    }
  }

  return StatusCode::SUCCESS;
}


StatusCode CaloCellMaker::finalize()
{
  for ( auto tool : m_toolHandles )
  {
    if (tool->finalize().isFailure() )
    {
      MSG_ERROR( "It's not possible to finalize " << tool->name() << " tool." );
    }
  }
  return StatusCode::SUCCESS;
}


StatusCode CaloCellMaker::bookHistograms( SG::EventContext &ctx ) const
{
  auto store = ctx.getStoreGateSvc();

  store->mkdir(m_histPath);
  store->add( new TH1F("res_layer" ,"(#E_{Estimated}-#E_{Truth})/#E_{Truth};res_{E};Count",100,-40,40) );

  // Create the 2D histogram for monitoring purpose
  store->add(new TH2F( "cells", "Estimated Cells Energy; #eta; #phi; Energy [MeV]", m_eta_bins, m_eta_min, m_eta_max, 
                       m_phi_bins, m_phi_min, m_phi_max) );

  // Create the 2D histogram for monitoring purpose
  store->add(new TH2F( "truth_cells", "Truth Cells Energy; #eta; #phi; Energy [MeV]", m_eta_bins, m_eta_min, m_eta_max, 
                         m_phi_bins, m_phi_min, m_phi_max) );


  if (m_detailedHistograms){
    int nbunchs = m_bcid_end - m_bcid_start + 1;
    store->add(new TH2F( "energy_samples_per_bunch", "", nbunchs, m_bcid_start, m_bcid_end+1, 100, 0, 3.5) );
    store->add(new TH2F( "timesteps", "Time steps; time [ns]; Energy [MeV];", nbunchs*100, m_bcid_start*(m_bc_duration-0.5), m_bcid_end*(m_bc_duration+0.5), 
                         30, 0, 30) );
  }


  return StatusCode::SUCCESS;
}


StatusCode CaloCellMaker::pre_execute( EventContext &ctx ) const
{
  // Build the CaloCellCollection and attach into the EventContext
  // Create the cell collection into the event context
  SG::WriteHandle<xAOD::CaloCellCollection> collection( m_collectionKey, ctx );
  
  collection.record( std::unique_ptr<xAOD::CaloCellCollection>(new xAOD::CaloCellCollection(m_eta_min,m_eta_max,m_eta_bins,m_phi_min,
                                                                                            m_phi_max,m_phi_bins,m_rmin,m_rmax,
                                                                                            (CaloSample)m_sampling)));
  // Read the file:
  std::ifstream file( m_caloCellFile );

	std::string line;
	while (std::getline(file, line))
	{
    std::string command;
    // Get the command
    file >> command;
    // Get only cell config 
    if (command=="C"){
      float  eta, phi, deta, dphi, rmin, rmax;
      int sampling, section; // Calorimeter layer and eta/phi ids
      unsigned int hash;
      //std::string hash;
      file >> sampling >> eta >> phi >> deta >> dphi >> rmin >> rmax >> hash;

      // Create the calorimeter cell
      auto *cell = new xAOD::RawCell( eta, phi, deta, dphi, rmin, rmax, hash, (CaloSample)sampling,
                                      m_bc_duration, m_bc_nsamples, m_bcid_start, m_bcid_end, m_bcid_truth);
      
      // Add the CaloCell into the collection
      collection->push_back( cell );
    } 
	}
  file.close();
  return StatusCode::SUCCESS;
}

 
StatusCode CaloCellMaker::execute( EventContext &ctx , const G4Step *step ) const
{
  //MSG_INFO( "AKI" );
  SG::ReadHandle<xAOD::CaloCellCollection> collection( m_collectionKey, ctx );

  //MSG_INFO( "AKI 1" );

  if( !collection.isValid() ){
    MSG_FATAL("It's not possible to retrieve the CaloCellCollection using this key: " << m_collectionKey);
  }
  //MSG_INFO( "AKI 2" );

  // Get the position
  G4ThreeVector pos = step->GetPreStepPoint()->GetPosition();
  // Apply all necessary transformation (x,y,z) to (eta,phi,r) coordinates
  // Get ATLAS coordinates (in transverse plane xy)
  auto vpos = TVector3( pos.x(), pos.y(), pos.z());

  // This object can not be const since we will change the intenal value
  xAOD::RawCell *cell=nullptr;
  //MSG_INFO( "AKI 3" );
  collection->retrieve( vpos, cell );
  //MSG_INFO( "AKI 4" );
  
  if(cell) cell->Fill( step );
  //MSG_INFO( "AKI 5" );

  return StatusCode::SUCCESS;
}


StatusCode CaloCellMaker::post_execute( EventContext &ctx ) const
{
  SG::ReadHandle<xAOD::CaloCellCollection> collection( m_collectionKey, ctx );
 
  if( !collection.isValid() ){
    MSG_FATAL("It's not possible to retrieve the CaloCellCollection using this key: " << m_collectionKey);
  }

  // Event info
  SG::ReadHandle<xAOD::EventInfoContainer> event(m_eventKey, ctx);
  
  if( !event.isValid() ){
    MSG_FATAL( "It's not possible to read the xAOD::EventInfoContainer from this Context" );
  }

  auto evt = (**event.ptr()).front();

  for ( const auto& p : **collection.ptr() )
  {
    for ( auto tool : m_toolHandles )
    {
      if( tool->executeTool( evt, p.second ).isFailure() ){
        MSG_ERROR( "It's not possible to execute the tool with name " << tool->name() );
        return StatusCode::FAILURE;
      }
    }
  }
 

  return StatusCode::SUCCESS;
}


StatusCode CaloCellMaker::fillHistograms( EventContext &ctx ) const
{
  auto store = ctx.getStoreGateSvc();
  SG::ReadHandle<xAOD::CaloCellCollection> collection( m_collectionKey, ctx );
 
  if( !collection.isValid() ){
    MSG_FATAL("It's not possible to retrieve the CaloCellCollection using this key: " << m_collectionKey);
  }



  for ( const auto& p : **collection.ptr() ){ 
    const auto *cell = p.second;

    store->cd(m_histPath);
    store->hist1("res_layer")->Fill( (cell->energy()-cell->truthRawEnergy())/cell->truthRawEnergy() );


    {// Fill estimated energy 2D histograms
      int x = store->hist2("cells")->GetXaxis()->FindBin(cell->eta());
      int y = store->hist2("cells")->GetYaxis()->FindBin(cell->phi());
      int bin = store->hist2("cells")->GetBin(x,y,0);
      float energy = store->hist2("cells")->GetBinContent( bin );
      store->hist2("cells")->SetBinContent( bin, (energy + cell->energy()) );
  
      if(m_detailedHistograms){
        int i=0;
        auto samples = cell->rawEnergySamples();
        for ( int bc=m_bcid_start; bc<m_bcid_end+1; ++bc)
        {
          store->hist2("energy_samples_per_bunch")->Fill(bc,samples[i]/1000.);
          ++i;
        }
      }

    }
    
    {// Fill truth energy 2D histograms
      int x = store->hist2("truth_cells")->GetXaxis()->FindBin(cell->eta());
      int y = store->hist2("truth_cells")->GetYaxis()->FindBin(cell->phi());
      int bin = store->hist2("truth_cells")->GetBin(x,y,0);
      float energy = store->hist2("truth_cells")->GetBinContent( bin );
      store->hist2("truth_cells")->SetBinContent( bin, (energy + cell->truthRawEnergy()) );
    }


  }
  return StatusCode::SUCCESS;
}



