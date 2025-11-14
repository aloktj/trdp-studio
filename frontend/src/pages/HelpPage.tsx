export function HelpPage() {
  return (
    <div>
      <h1>Help &amp; Reference</h1>
      <section className="card">
        <p>
          Use the navigation menu to configure TRDP XML files, network settings, and monitor PD/MD traffic. Refer to the
          README for the full workflow once the engine integration is ready.
        </p>
        <ul>
          <li>TRDP Config – manage XML definitions and activate them.</li>
          <li>Network – set the runtime interface, IP, and multicast groups.</li>
          <li>TRDP Comms – inspect PD/MD messages once the backend provides data.</li>
        </ul>
      </section>
    </div>
  );
}
