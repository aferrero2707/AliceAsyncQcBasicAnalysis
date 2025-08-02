#include <QualityControl/MonitorObject.h>
#include <QualityControl/MonitorObjectCollection.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <set>

#include <chrono>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

using namespace o2::quality_control::core;

struct RunsConfig
{
  std::string sessionID;
  std::string recoType;
  std::string year;
  std::string period;
  std::string pass;
  std::string beamType;
  std::vector<std::string> rootFiles;
};

RunsConfig runsConfig;
RunsConfig runsConfigRef;

using namespace o2::quality_control::core;

struct PlotConfig
{
  std::string detectorName;
  std::string taskName;
  std::string plotName;
  std::string plotLabel;
  std::string projection;
  std::string drawOptions;
  bool logx;
  bool logy;
  double checkRangeMin;
  double checkRangeMax;
  double checkThreshold;
  double checkDeviationNsigma;
  double maxBadBinsFrac;
  int rebin;
  bool normalize;
};

struct Plot
{
  //Plot(std::shared_ptr<TH1> h, Activity& a, ValidityInterval& v)
  //: histogram(h), activity(a), validity(v) {}

  Plot(TH1* h, Activity a, ValidityInterval v)
  : histogram(h), activity(a), validity(v) {}

  std::shared_ptr<TH1> histogram;
  Activity activity;
  ValidityInterval validity;
};

struct Canvas
{
  std::shared_ptr<TCanvas> canvas;
  std::shared_ptr<TPad> padTop;
  std::shared_ptr<TPad> padBottom;
  //std::shared_ptr<TPad> padRight;
};

std::string getPlotOutputFilePath(const PlotConfig& plotConfig, int targetRun = 0)
{
  std::string plotNameWithDashes = plotConfig.plotName;
  std::replace( plotNameWithDashes.begin(), plotNameWithDashes.end(), '/', '-');

  std::string outputPath = (targetRun == 0) ?
      std::string("outputs/") + runsConfig.sessionID + "/" + runsConfig.year + "/" + runsConfig.period + "/" + runsConfig.pass + "/" :
      std::string("outputs/") + runsConfig.sessionID + "/" + runsConfig.year + "/" + runsConfig.period + "/" + runsConfig.pass + "/" + std::to_string(targetRun) + "/";

  return outputPath;
}

std::string getPlotOutputFilePrefix(const PlotConfig& plotConfig, int targetRun = 0)
{
  std::string plotNameWithDashes = plotConfig.plotName;
  std::replace( plotNameWithDashes.begin(), plotNameWithDashes.end(), '/', '-');

  std::string outputFileName = getPlotOutputFilePath(plotConfig, targetRun) + plotConfig.detectorName + "-" + plotConfig.taskName + "-" + plotNameWithDashes + "-comp";

  if (!plotConfig.projection.empty()) {
    outputFileName += std::string("-proj") + plotConfig.projection;
  }

  return outputFileName;
}

bool splitPlotPath(std::string plotPath, std::array<std::string, 4>& plotPathSplitted)
{
  std::string delimiter("/");

  for (int i = 0; i < 3; i++) {
    auto index = plotPath.find(delimiter);
    if (index == std::string::npos) {
      return false;
    }
    plotPathSplitted[i] = plotPath.substr(0, index);
    plotPath.erase(0, index + 1);
  }
  plotPathSplitted[3] = plotPath;

  //for (auto s : plotPathSplitted) { std::cout << s << " "; } std::cout << std::endl;

  return true;
}

TDirectory* GetDir(TDirectory* d, TString histname)
{
  //TString histname = TString::Format("ST%d/DE%d/Occupancy_B_XY_%d", station, de, de);
  TKey *key = d->GetKey(histname);
  //std::cout << "dirName: " << histname << "  key: " <<key << std::endl;
  if (!key) return NULL;
  TDirectory* dir = (TDirectory*)key->ReadObjectAny(TDirectory::Class());
  //std::cout << "dirName: " << histname << "  dir: " << dir << std::endl;
  return dir;
}

MonitorObjectCollection* GetMOC(TDirectory* f, TString histname)
{
  //TString histname = TString::Format("ST%d/DE%d/Occupancy_B_XY_%d", station, de, de);
  TKey *key = f->GetKey(histname);
  //std::cout << "MOCname: " << histname << "  key: " <<key << std::endl;
  if (!key) return NULL;
  auto* moc = (MonitorObjectCollection*)key->ReadObjectAny(MonitorObjectCollection::Class());
  //std::cout << "MOCname: " << histname << "  moc: " << moc << std::endl;
  return moc;
}

MonitorObject* GetMO(TFile* f, std::array<std::string, 4>& path)
{
  TDirectory* dir = GetDir(f, path[0].c_str());
  if (!dir) {
    std::cout << "Directory \"mw\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return nullptr;
  }
  dir = GetDir(dir, path[1].c_str());
  if (!dir) {
    std::cout << "Directory \"" << path[1] << "\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return nullptr;
  }
  auto* moc = GetMOC(dir, path[2].c_str());
  if (!moc) {
    std::cout << "MOC \"" << path[2] << "\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return nullptr;
  }
  auto* mo = (MonitorObject*)moc->FindObject(path[3].c_str());
  //std::cout << "mo: " << mo << std::endl;
  //if (mo) {
  //  std::cout << "  run number: " << mo->getActivity().mId << std::endl;
  //  std::cout << "  validity: " << mo->getValidity().getMin() << " -> " << mo->getValidity().getMax() << std::endl;
  //}
  return mo;
}

MonitorObject* GetMO(TFile* f, const PlotConfig& plotConfig)
{
  std::string fullPath = std::string("int/") +
      plotConfig.detectorName + "/" +
      plotConfig.taskName + "/" +
      plotConfig.plotName;
  std::array<std::string, 4> splittedPath;
  splitPlotPath(fullPath, splittedPath);
  return GetMO(f, splittedPath);
}

void loadPlotsFromRootFiles(std::vector<std::shared_ptr<TFile>>& rootFiles, const PlotConfig& plotConfig,
    std::map<int, std::shared_ptr<MonitorObject>>& monitorObjects)
{

  for (auto rootFile : rootFiles) {
    //std::cout << "Loading plot \"" << plotConfig.plotName << "\" from file " << rootFile->GetPath() << std::endl;
    std::string fullPath = std::string("int/") +
        plotConfig.detectorName + "/" +
        plotConfig.taskName + "/" +
        plotConfig.plotName;
    auto mo = GetMO(rootFile.get(), plotConfig);
    if (!mo) {
      std::cout << "  Failed to load MO \"" << fullPath << "\" from file " << rootFile->GetPath() << std::endl;
      continue;
    }
    int runNumber = mo->getActivity().mId;
    monitorObjects[runNumber].reset(mo);
    std::cout << "Loaded MO \"" << fullPath << "\" from file " << rootFile->GetPath() << std::endl;
  }
}

void InitCanvas(Canvas& canvas)
{
  int cW = 1800;
  int cH = 1200;
  double topBottomRatio = 1;
  double topSize = topBottomRatio / (topBottomRatio + 1.0);
  double bottomSize = 1.0 / (topBottomRatio + 1.0);

  canvas.canvas = std::make_shared<TCanvas>("c","c",cW,cH);

  canvas.padTop = std::make_shared<TPad>("pad_top", "Top Pad", 0, 0, 3.0 / 3.0, 1);
  canvas.padTop->SetBottomMargin(bottomSize);
  canvas.padTop->SetRightMargin(0);
  canvas.padTop->SetFillStyle(4000); // transparent
  canvas.canvas->cd();
  canvas.padTop->Draw();

  canvas.padBottom = std::make_shared<TPad>("pad_bottom", "Bottom Pad", 0, 0, 3.0 / 3.0, 1);
  canvas.padBottom->SetTopMargin(topSize * 1.0);
  canvas.padBottom->SetRightMargin(0);
  canvas.padBottom->SetFillStyle(4000); // transparent
  canvas.canvas->cd();
  canvas.padBottom->Draw();

  //canvas.padRight = std::make_shared<TPad>("pad_right", "Right Pad", 2.0 / 3.0, 0, 1, 1);
  //canvas.padRight->SetFillStyle(4000); // transparent
  //canvas.canvas->cd();
  //canvas.padRight->Draw();
}

TH1* GetTH1(std::shared_ptr<MonitorObject> mo, std::string projection, std::string suffix)
{
  TH1* hist{ nullptr };
  TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
  //std::cout << "histTemp: " << histTemp << "  entries: " << histTemp->GetEntries() << std::endl;
  if (!histTemp) return nullptr;

  // Convert TProfile plots into histograms to get correct errors for the ratios
  if (dynamic_cast<TProfile*>(histTemp)) {
    TProfile* hp = dynamic_cast<TProfile*>(histTemp);
    hist = hp->ProjectionX((std::string(histTemp->GetName()) + "_profile_px" + suffix).c_str());
  } else if (projection == "x") {
    TH2* h2 = dynamic_cast<TH2*>(histTemp);
    if (h2) {
      hist = (TH1*)h2->ProjectionX((std::string(histTemp->GetName()) + "_px" + suffix).c_str());
    }
  } else if (projection == "y") {
    TH2* h2 = dynamic_cast<TH2*>(histTemp);
    if (h2) {
      hist = (TH1*)h2->ProjectionY((std::string(histTemp->GetName()) + "_py" + suffix).c_str());
    }
  } else {
    hist = (TH1*)histTemp->Clone((std::string(histTemp->GetName()) + suffix).c_str());
  }

  return hist;
}

double getNormalizationFactor(TH1* hist, double xmin, double xmax)
{
  if (xmin != xmax) {
    int binMin = hist->GetXaxis()->FindBin(xmin);
    int binMax = hist->GetXaxis()->FindBin(xmax);
    double integral = hist->Integral(binMin, binMax);
    return ((integral == 0) ? 1.0 : 1.0 / integral);
  } else {
    double integral = hist->Integral();
    return ((integral == 0) ? 1.0 : 1.0 / integral);
  }
}

void normalizeHistogram(TH1* hist, double xmin, double xmax)
{
  hist->Scale(getNormalizationFactor(hist, xmin, xmax));
}

std::set<int> plotRunsWithRatios(const PlotConfig& plotConfig,
    std::map<int, std::shared_ptr<MonitorObject>>& monitorObjects,
    std::map<int, std::shared_ptr<MonitorObject>>& monitorObjectsRef,
    int targetRun = 0)
{
  double checkRangeMin = plotConfig.checkRangeMin;
  double checkRangeMax = plotConfig.checkRangeMax;
  double checkThreshold = plotConfig.checkThreshold;
  double checkDeviationNsigma = plotConfig.checkDeviationNsigma;
  double chekMaxBadBinsFrac = plotConfig.maxBadBinsFrac;
  bool logx = plotConfig.logx;
  bool logy = plotConfig.logy;
  auto projection = plotConfig.projection;
  int rebin = plotConfig.rebin;
  bool normalize = plotConfig.normalize;

  std::set<int> badRuns;

  //int cW = 1800;
  //int cH = 1200;
  float labelSize = 0.025;
  //double topBottomRatio = 1;
  //double topSize = topBottomRatio / (topBottomRatio + 1.0);
  //double bottomSize = 1.0 / (topBottomRatio + 1.0);

  Canvas canvas;
  InitCanvas(canvas);

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig, targetRun) + ".pdf";
  //std::cout << "Creating folder \"" << getPlotOutputFilePath(plotConfig, targetRun) << "\"" << std::endl;
  gSystem->mkdir(getPlotOutputFilePath(plotConfig, targetRun).c_str(), kTRUE);

  bool firstPage = true;
  for (auto& [index, mo] : monitorObjects) {
    if (!mo) continue;

    bool hasPlotsInIndex = false;
    if (targetRun == 0) {
      hasPlotsInIndex = true;
    } else {
      if (mo->getActivity().mId == targetRun) {
        hasPlotsInIndex = true;
      }
    }

    if (!hasPlotsInIndex) continue;

    int runNumber = mo->getActivity().mId;
    if (monitorObjectsRef.count(runNumber) < 1) {
      std::cout << "Could not find reference MO \"" << mo->GetName() << "\" for run " << runNumber << std::endl;
      continue;
    }

    std::shared_ptr<MonitorObject> moRef = monitorObjectsRef[runNumber];

    canvas.padTop->Clear();
    canvas.padBottom->Clear();
    canvas.padTop->cd();

    // log scales
    if (logx) {
      canvas.padTop->SetLogx(kTRUE);
    } else {
      canvas.padTop->SetLogx(kFALSE);
    }
    if (logy) {
      canvas.padTop->SetLogy(kTRUE);
    } else {
      canvas.padTop->SetLogy(kFALSE);
    }


    TH1* histCurrent = GetTH1(mo, projection, std::string("_comp_") + std::to_string(index) + "_" + std::to_string(targetRun));
    if (rebin > 1)
      histCurrent->Rebin(rebin);
    if (normalize) {
      normalizeHistogram(histCurrent, checkRangeMin, checkRangeMax);
      histCurrent->GetYaxis()->SetTitle("A.U.");
    }

    TH1* histReference = GetTH1(moRef, projection, std::string("_comp_ref_") + std::to_string(index) + "_" + std::to_string(targetRun));
    if (rebin > 1)
      histReference->Rebin(rebin);
    if (normalize) {
      normalizeHistogram(histReference, checkRangeMin, checkRangeMax);
    }

    histCurrent->GetXaxis()->SetLabelSize(0);
    histCurrent->GetXaxis()->SetTitleSize(0);
    histCurrent->GetYaxis()->SetLabelSize(labelSize);
    histCurrent->GetYaxis()->SetTitleSize(labelSize);
    histCurrent->SetLineColor(kBlack);

    histCurrent->SetTitle(TString::Format("%s - run %d", histCurrent->GetTitle(), runNumber));
    histCurrent->Draw((plotConfig.drawOptions + "").c_str());
    histReference->SetLineStyle(kDashed);
    histReference->SetLineColor(kRed);
    histReference->Draw((plotConfig.drawOptions + " same").c_str());

    double fracBad = 0;
    canvas.padBottom->cd();

    // log scale
    if (logx) {
      canvas.padBottom->SetLogx(kTRUE);
    } else {
      canvas.padBottom->SetLogx(kFALSE);
    }

     TH1* histRatio = GetTH1(mo, projection, std::string("_ratio_") + std::to_string(index) + "_" + std::to_string(targetRun));
    if (rebin > 1) {
      histRatio->Rebin(rebin);
    }
    if (normalize) {
      normalizeHistogram(histRatio, checkRangeMin, checkRangeMax);
    }

    histRatio->Divide(histReference);
    histRatio->SetTitle("");
    histRatio->SetTitleSize(0);
    histRatio->GetXaxis()->SetLabelSize(labelSize);
    histRatio->GetXaxis()->SetTitleSize(labelSize);
    histRatio->GetYaxis()->SetTitle("ratio");
    histRatio->GetYaxis()->CenterTitle(kTRUE);
    histRatio->GetYaxis()->SetNdivisions(5);
    histRatio->GetYaxis()->SetLabelSize(labelSize);
    histRatio->GetYaxis()->SetTitleSize(labelSize);

    histRatio->SetLineColor(kBlack);

    //histRatio->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
    histRatio->Draw("H");
    histRatio->SetMinimum(0.5 + 1.0e-3);
    histRatio->SetMaximum(1.5 - 1.0e-3);
    //histRatio->SetMinimum(0.8 + 1.0e-3);
    //histRatio->SetMaximum(1.2 - 1.0e-3);

    // check quality
    double nBinsChecked = 0;
    double nBinsBad = 0;
    for (int bin = 1; bin <= histRatio->GetXaxis()->GetNbins(); bin++) {
      double xBin = histRatio->GetXaxis()->GetBinCenter(bin);
      if (checkRangeMin != checkRangeMax) {
        if (xBin < checkRangeMin || xBin > checkRangeMax) {
          continue;
        }
      }

      nBinsChecked += 1;
      double ratio = histRatio->GetBinContent(bin);
      double error = histRatio->GetBinError(bin);
      double deviation = std::fabs(ratio - 1.0);
      double threshold = checkThreshold + error * checkDeviationNsigma;
      if (deviation > threshold) {
        nBinsBad += 1;
      }
    }
    fracBad = (nBinsChecked > 0) ? (nBinsBad / nBinsChecked) : 0;
    if (fracBad > chekMaxBadBinsFrac) {
      //std::cout << "Bad time interval for plot \"" << plotConfig.plotName << "\": "
      //    << TString::Format("%d [%02d:%02d:%02d - %02d:%02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax).Data()
      //    << TString::Format(" - IR: [%0.1f kHz, %0.1f kHz]", rateIntervals[index].first, rateIntervals[index].second)
      //    << std::endl;

      badRuns.insert(mo->getActivity().mId);
    }

    canvas.padBottom->cd();

    double lineXmin = (checkRangeMin != checkRangeMax) ? checkRangeMin : histReference->GetXaxis()->GetXmin();
    double lineXmax = (checkRangeMin != checkRangeMax) ? checkRangeMax : histReference->GetXaxis()->GetXmax();
    TLine* lineMin = new TLine(lineXmin, 1.0 - checkThreshold, lineXmax, 1.0 - checkThreshold);
    lineMin->SetLineColor(kRed);
    lineMin->SetLineStyle(7);
    lineMin->SetLineWidth(2);
    TLine* lineMax = new TLine(lineXmin, 1.0 + checkThreshold, lineXmax, 1.0 + checkThreshold);
    lineMax->SetLineColor(kRed);
    lineMax->SetLineStyle(7);
    lineMax->SetLineWidth(2);

    lineMin->Draw();
    lineMax->Draw();

    if (firstPage) canvas.canvas->SaveAs((outputFileName + "(").c_str());
    else canvas.canvas->SaveAs(outputFileName.c_str());

    firstPage = false;
  }
  canvas.canvas->Clear();
  canvas.canvas->SaveAs((outputFileName + ")").c_str());

  return badRuns;
}

void aqc_compare(const char* runsConfigFile, const char* runsConfigRefFile, const char* plotsConfig)
{
  gStyle->SetOptStat(0);
  gStyle->SetOptFit(1111);
  gStyle->SetPalette(57, 0);
  gStyle->SetNumberContours(40);

  std::ifstream fRunsConfig(runsConfigFile);
  auto jRunsConfig = json::parse(fRunsConfig);

  std::ifstream fRunsConfigRef(runsConfigRefFile);
  auto jRunsConfigRef = json::parse(fRunsConfigRef);

  std::ifstream fPlotsConfig(plotsConfig);
  auto jPlotsConfig = json::parse(fPlotsConfig);
  runsConfig.sessionID = jPlotsConfig.at("id").get<std::string>();
  std::cout << "ID: " << runsConfig.sessionID << std::endl;

  runsConfig.recoType = jRunsConfig.at("type").get<std::string>();
  runsConfig.year = jRunsConfig.at("year").get<std::string>();
  runsConfig.period = jRunsConfig.at("period").get<std::string>();
  runsConfig.pass = jRunsConfig.at("pass").get<std::string>();
  runsConfig.beamType = jRunsConfig.at("beamType").get<std::string>();
  try {
    runsConfig.rootFiles = jRunsConfig.at("rootFiles").get<std::vector<std::string>>();
  } catch(const std::exception& e) {
    runsConfig.rootFiles.push_back("QC_fullrun.root");
  }

  runsConfigRef.recoType = jRunsConfigRef.at("type").get<std::string>();
  runsConfigRef.year = jRunsConfigRef.at("year").get<std::string>();
  runsConfigRef.period = jRunsConfigRef.at("period").get<std::string>();
  runsConfigRef.pass = jRunsConfigRef.at("pass").get<std::string>();
  runsConfigRef.beamType = jRunsConfigRef.at("beamType").get<std::string>();
  try {
    runsConfigRef.rootFiles = jRunsConfigRef.at("rootFiles").get<std::vector<std::string>>();
  } catch(const std::exception& e) {
    runsConfigRef.rootFiles.push_back("QC_fullrun.root");
  }

  // input runs
  std::vector<int> inputRuns = jRunsConfig.at("runs");
  std::vector<int> runNumbers;
  std::vector<int> runNumbersAll;
  for (const auto& inputRun : inputRuns) {
    runNumbers.push_back(inputRun);
    runNumbersAll.push_back(inputRun);
  }

  // Plot configuration
  std::vector<PlotConfig> plotConfigsVector;

  if (jPlotsConfig.count("plots") > 0) {
    auto plotConfigs = jPlotsConfig.at("plots");
    std::cout << "plotConfigs.size(): " << plotConfigs.size() << std::endl;
    for (const auto& config : plotConfigs) {
      auto detectorName = config.at("detector").get<std::string>();
      auto taskName = config.at("task").get<std::string>();
      auto plotName = config.at("name").get<std::string>();
      std::cout << "New plot: \"" << detectorName << "/" << taskName << "/" << plotName << "\"" << std::endl;
      plotConfigsVector.push_back({ detectorName, taskName, plotName,
                        config.value("label", ""),
                        config.value("projection", ""),
                        config.value("drawOptions", "H"),
                        config.value("logx", false),
                        config.value("logy", false),
                        config.value("checkRangeMin", double(0.0)),
                        config.value("checkRangeMax", double(0.0)),
                        config.value("checkThreshold", double(0.1)),
                        config.value("checkDeviationNsigma", double(2.0)),
                        config.value("maxBadBinsFrac", double(0.1)),
                        config.value("rebin", 1),
                        config.value("normalize", true)
      });
    }
  } else {
    std::cout << "Key \"" << "plots" << "\" not found in configuration" << std::endl;
  }

  // loading of ROOT files
  std::vector<std::string> rootFileNames;
  std::vector<std::shared_ptr<TFile>> rootFiles;
  for (auto runNumber : runNumbersAll) {
    std::cout << "  run " << runNumber << std::endl;
    std::string inputFilePath = std::string("inputs/") + runsConfig.year + "/" + runsConfig.period + "/" + runsConfig.pass + "/"
        + std::to_string(runNumber) + "/";
    for (auto rootFileName : runsConfig.rootFiles) {
      auto fullPath = inputFilePath + rootFileName;
      std::shared_ptr<TFile> rootFile(TFile::Open(fullPath.c_str()));
      if (!rootFile) {
        std::cout << "    Input ROOT file \"" << fullPath << "\" not found" << std::endl;
        continue;
      }
      rootFileNames.push_back(fullPath);
      rootFiles.push_back(rootFile);
      std::cout << "    Input ROOT file \"" << fullPath << "\" added to run " << runNumber << std::endl;
    }
  }

  std::vector<std::string> rootFileNamesRef;
  std::vector<std::shared_ptr<TFile>> rootFilesRef;
  for (auto runNumber : runNumbersAll) {
    std::cout << "  run " << runNumber << std::endl;
    std::string inputFilePath = std::string("inputs/") + runsConfigRef.year + "/" + runsConfigRef.period + "/" + runsConfigRef.pass + "/"
        + std::to_string(runNumber) + "/";
    for (auto rootFileName : runsConfigRef.rootFiles) {
      auto fullPath = inputFilePath + rootFileName;
      std::shared_ptr<TFile> rootFile(TFile::Open(fullPath.c_str()));
      if (!rootFile) {
        std::cout << "    Reference input ROOT file \"" << fullPath << "\" not found" << std::endl;
        continue;
      }
      rootFileNamesRef.push_back(fullPath);
      rootFilesRef.push_back(rootFile);
      std::cout << "    Reference input ROOT file \"" << fullPath << "\" added to run " << runNumber << std::endl;
    }
  }

  for (const auto& plot : plotConfigsVector) {
    std::map<int, std::multimap<double, std::shared_ptr<Plot>>> plots;
    std::map<int, std::shared_ptr<MonitorObject>> monitorObjects;
    std::map<int, std::shared_ptr<MonitorObject>> monitorObjectsRef;

    loadPlotsFromRootFiles(rootFiles, plot, monitorObjects);
    loadPlotsFromRootFiles(rootFilesRef, plot, monitorObjectsRef);

    auto badRuns = plotRunsWithRatios(plot, monitorObjects, monitorObjectsRef);
  }
}
