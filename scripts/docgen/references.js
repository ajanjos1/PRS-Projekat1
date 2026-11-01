// Literatura u IEEE stilu. Numeracija se dodjeljuje redoslijedom prvog citiranja (cite).
// Svi podaci provjereni (Crossref/OpenAlex/izdavač/arXiv), vidi references/bibliography.bib.
const REFS = {
  mei2007: 'X. Mei, P. Decaudin, and B.-G. Hu, "Fast hydraulic erosion simulation and visualization on GPU," in *Proc. 15th Pacific Conf. Computer Graphics and Applications (PG\'07)*, 2007, pp. 47–56, doi: 10.1109/PG.2007.15.',
  stava2008: 'O. Šťava, B. Beneš, M. Brisbin, and J. Křivánek, "Interactive terrain modeling using hydraulic erosion," in *Proc. Eurographics/SIGGRAPH Symp. Computer Animation (SCA)*, M. Gross and D. James, Eds., 2008, pp. 201–210, doi: 10.2312/SCA/SCA08/201-210.',
  bates2010: 'P. D. Bates, M. S. Horritt, and T. J. Fewtrell, "A simple inertial formulation of the shallow water equations for efficient two-dimensional flood inundation modelling," *J. Hydrology*, vol. 387, no. 1–2, pp. 33–45, 2010, doi: 10.1016/j.jhydrol.2010.03.027.',
  neal2018: 'J. Neal, T. Dunne, C. Sampson, A. Smith, and P. Bates, "Optimisation of the two-dimensional hydraulic model LISFOOD-FP for CPU architecture," *Environ. Model. Softw.*, vol. 107, pp. 148–157, 2018, doi: 10.1016/j.envsoft.2018.05.011.',
  nilles2024: 'A. M. Nilles, L. Günther, T. Wagner, and S. Müller, "3D real-time hydraulic erosion simulation using multi-layered heightmaps," in *Vision, Modeling, and Visualization (VMV 2024)*, L. Linsen and J. Thies, Eds. Eurographics Association, 2024, doi: 10.2312/vmv.20241211. [Online]. Available: https://diglib.eg.org/handle/10.2312/vmv20241211',
  salamin2024: 'B. Salamin and S. Szénási, "Simulating hidraulic erosion utilizing graphics processors," in *Proc. IEEE 22nd Jubilee Int. Symp. Intelligent Systems and Informatics (SISY)*, 2024, pp. 309–314, doi: 10.1109/SISY62279.2024.10737605. [Online]. Available: https://szfi.nik.uni-obuda.hu/storage/app/media/szakirany-publikaciok/salamin_szenasi.pdf',
  schott2023: 'H. Schott, A. Paris, L. Fournier, E. Guérin, and E. Galin, "Large-scale terrain authoring through interactive erosion simulation," *ACM Trans. Graph.*, vol. 42, no. 5, pp. 1–15, 2023, doi: 10.1145/3592787.',
  schott2024: 'H. Schott, E. Galin, E. Guérin, A. Paris, and A. Peytavie, "Terrain amplification using multi scale erosion," *ACM Trans. Graph.*, vol. 43, no. 4, pp. 1–12, 2024, doi: 10.1145/3658200.',
  jain2024debris: 'A. Jain, B. Benes, and G. Cordonnier, "Efficient debris-flow simulation for steep terrain erosion," *ACM Trans. Graph.*, vol. 43, no. 4, pp. 1–11, 2024, doi: 10.1145/3658213.',
  jain2024fastflow: 'A. Jain, B. Kerbl, J. Gain, B. Finley, and G. Cordonnier, "FastFlow: GPU acceleration of flow and depression routing for landscape simulation," *Comput. Graph. Forum*, vol. 43, no. 7, Art. no. e15243, 2024, doi: 10.1111/cgf.15243.',
  shaw2021: 'J. Shaw, G. Kesserwani, J. Neal, P. Bates, and M. K. Sharifian, "LISFLOOD-FP 8.0: The new discontinuous Galerkin shallow-water solver for multi-core CPUs and GPUs," *Geosci. Model Dev.*, vol. 14, no. 6, pp. 3577–3602, 2021, doi: 10.5194/gmd-14-3577-2021.',
  sharifian2023: 'M. K. Sharifian, G. Kesserwani, A. A. Chowdhury, J. Neal, and P. Bates, "LISFLOOD-FP 8.1: New GPU-accelerated solvers for faster fluvial/pluvial flood simulations," *Geosci. Model Dev.*, vol. 16, no. 9, pp. 2391–2413, 2023, doi: 10.5194/gmd-16-2391-2023.',
  caviedes2023: 'D. Caviedes-Voullième, M. Morales-Hernández, M. R. Norman, and I. Özgen-Xian, "SERGHEI (SERGHEI-SWE) v1.0: A performance-portable high-performance parallel-computing shallow-water solver for hydrology and environmental hydraulics," *Geosci. Model Dev.*, vol. 16, no. 3, pp. 977–1008, 2023, doi: 10.5194/gmd-16-977-2023.',
  morales2021: 'M. Morales-Hernández, M. B. Sharif, A. Kalyanapu, S. K. Ghafoor, T. T. Dullo, S. Gangrade, S.-C. Kao, M. R. Norman, and K. J. Evans, "TRITON: A multi-GPU open source 2D hydrodynamic flood model," *Environ. Model. Softw.*, vol. 141, Art. no. 105034, 2021, doi: 10.1016/j.envsoft.2021.105034.',
  steffen2022: 'L. Steffen, F. Amann, and R. Hinkelmann, "Performance improvement strategies for an FVM-based shallow water flow model on 2D structured grids," in *Proc. 39th IAHR World Congress*, 2022, pp. 3804–3815, doi: 10.3850/IAHR-39WC252171192022753.',
  klower2022: 'M. Klöwer, S. Hatfield, M. Croci, P. D. Düben, and T. N. Palmer, "Fluid simulations accelerated with 16 bits: Approaching 4x speedup on A64FX by squeezing ShallowWaters.jl into Float16," *J. Adv. Model. Earth Syst.*, vol. 14, no. 2, Art. no. e2021MS002684, 2022, doi: 10.1029/2021MS002684.',
  villalobos2025: 'J. Villalobos, D. Caviedes-Voullième, S. Rizzi, and E. Meneses, "Towards portability at scale: A cross-architecture performance evaluation of a GPU-enabled shallow water solver," in *Proc. IEEE/SBC 37th Int. Symp. Computer Architecture and High Performance Computing (SBAC-PAD)*, 2025, pp. 180–191, doi: 10.1109/SBAC-PAD66369.2025.00025.',
  hou2020: 'J. Hou, Y. Kang, C. Hu, Y. Tong, B. Pan, and J. Xia, "A GPU-based numerical model coupling hydrodynamical and morphological processes," *Int. J. Sediment Res.*, vol. 35, no. 4, pp. 386–394, 2020, doi: 10.1016/j.ijsrc.2020.02.005.',
  vanzo2021: 'D. Vanzo, S. Peter, L. Vonwiller, M. Bürgler, M. Weberndorfer, A. Siviglia, D. Conde, and D. F. Vetsch, "BASEMENT v3: A modular freeware for river process modelling over multiple computational backends," *Environ. Model. Softw.*, vol. 143, Art. no. 105102, 2021, doi: 10.1016/j.envsoft.2021.105102.',
  cea2024: 'L. Cea, O. García-Feal, G. Nord, G. Piton, and C. Legout, "Implementation of a GPU-enhanced multiclass soil erosion model based on the 2D shallow water equations in the software Iber," *Environ. Model. Softw.*, vol. 179, Art. no. 106098, 2024, doi: 10.1016/j.envsoft.2024.106098.',
};

const order = [];
function cite(...keys) {
  const nums = keys.map((k) => {
    if (!REFS[k]) throw new Error("Nepoznata referenca: " + k);
    let i = order.indexOf(k);
    if (i < 0) { order.push(k); i = order.length - 1; }
    return i + 1;
  });
  return "[" + nums.join("], [") + "]";
}

module.exports = { REFS, cite, order };
