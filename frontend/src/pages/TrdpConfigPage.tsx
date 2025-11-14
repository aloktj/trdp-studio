import { useEffect, useState } from 'react';
import type { FormEvent } from 'react';
import { apiGet, apiPost } from '../api/client';
import type { TrdpConfigDetail, TrdpConfigSummary } from '../types';

export function TrdpConfigPage() {
  const [configs, setConfigs] = useState<TrdpConfigSummary[]>([]);
  const [selected, setSelected] = useState<TrdpConfigDetail | null>(null);
  const [formName, setFormName] = useState('');
  const [formXml, setFormXml] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  useEffect(() => {
    refreshList();
  }, []);

  const refreshList = async () => {
    try {
      const data = await apiGet<{ configs: TrdpConfigSummary[] }>('/api/trdp/configs');
      setConfigs(data.configs);
      setSelected((current) => {
        if (!current) return current;
        const stillExists = data.configs.some((config) => config.id === current.id);
        return stillExists ? current : null;
      });
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const loadDetails = async (configId: number) => {
    try {
      setError(null);
      const data = await apiGet<{ config: TrdpConfigDetail }>(`/api/trdp/configs/${configId}`);
      setSelected(data.config);
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const handleCreate = async (event: FormEvent) => {
    event.preventDefault();
    setLoading(true);
    setError(null);
    setNotice(null);
    try {
      const result = await apiPost<{ config: TrdpConfigDetail }>('/api/trdp/configs', {
        name: formName,
        xml: formXml,
      });
      setFormName('');
      setFormXml('');
      await refreshList();
      setSelected(result.config);
      setNotice('Configuration saved');
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setLoading(false);
    }
  };

  const handleActivate = async (configId: number) => {
    try {
      setError(null);
      setNotice(null);
      await apiPost(`/api/trdp/configs/${configId}/activate`);
      setNotice(`Configuration ${configId} activated`);
    } catch (err) {
      setError((err as Error).message);
    }
  };

  return (
    <div>
      <h1>TRDP XML Configurations</h1>
      {error && <div className="error">{error}</div>}
      {notice && <div className="notice">{notice}</div>}
      <section className="card">
        <h2>Create configuration</h2>
        <form onSubmit={handleCreate} className="stack">
          <label>
            Name
            <input value={formName} onChange={(e) => setFormName(e.target.value)} required />
          </label>
          <label>
            XML content
            <textarea value={formXml} onChange={(e) => setFormXml(e.target.value)} rows={8} required />
          </label>
          <button type="submit" disabled={loading}>
            {loading ? 'Saving…' : 'Save config'}
          </button>
        </form>
      </section>

      <section className="card">
        <h2>Saved configs</h2>
        <table>
          <thead>
            <tr>
              <th>ID</th>
              <th>Name</th>
              <th>Status</th>
              <th>Created</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {configs.map((config) => (
              <tr key={config.id}>
                <td>{config.id}</td>
                <td>{config.name}</td>
                <td>{config.validation_status}</td>
                <td>{new Date(config.created_at).toLocaleString()}</td>
                <td>
                  <button type="button" onClick={() => loadDetails(config.id)}>
                    View
                  </button>
                  <button type="button" onClick={() => handleActivate(config.id)}>
                    Activate
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      {selected && (
        <section className="card">
          <h2>Config #{selected.id}</h2>
          <p>
            <strong>Name:</strong> {selected.name}
          </p>
          <p>
            <strong>Status:</strong> {selected.validation_status}
          </p>
          <pre className="code-block">{selected.xml}</pre>
        </section>
      )}
    </div>
  );
}
