import { useEffect, useState } from 'react';
import type { FormEvent } from 'react';
import { apiGet, apiPost } from '../api/client';
import type { NetworkConfig } from '../types';

const emptyConfig: NetworkConfig = {
  interface_name: '',
  local_ip: '',
  multicast_groups: [],
  pd_port: 17224,
  md_port: 17225,
};

export function NetworkPage() {
  const [config, setConfig] = useState<NetworkConfig | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [hasSavedConfig, setHasSavedConfig] = useState<boolean | null>(null);

  const safeConfig = config ?? emptyConfig;

  const applyConfig = (nextConfig: NetworkConfig | null) => {
    setConfig(nextConfig ?? emptyConfig);
    setHasSavedConfig(Boolean(nextConfig));
  };

  useEffect(() => {
    loadConfig();
  }, []);

  const loadConfig = async () => {
    try {
      const data = await apiGet<{ config: NetworkConfig | null }>('/api/network/config');
      applyConfig(data.config);
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const handleSubmit = async (event: FormEvent) => {
    event.preventDefault();
    setMessage(null);
    setError(null);
    try {
      const payload: NetworkConfig = {
        ...safeConfig,
        multicast_groups: safeConfig.multicast_groups.filter((group) => group.trim().length > 0),
      };
      const data = await apiPost<{ config: NetworkConfig | null }>('/api/network/config', payload);
      applyConfig(data.config);
      setMessage('Network configuration saved');
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const updateField = (field: keyof NetworkConfig, value: string) => {
    setConfig((prev) => ({
      ...(prev ?? emptyConfig),
      [field]: field === 'pd_port' || field === 'md_port' ? Number(value) : value,
    }));
  };

  const updateGroups = (value: string) => {
    setConfig((prev) => ({
      ...(prev ?? emptyConfig),
      multicast_groups: value.split(',').map((entry) => entry.trim()),
    }));
  };

  return (
    <div>
      <h1>Network Configuration</h1>
      {error && <div className="error">{error}</div>}
      {hasSavedConfig === false && (
        <div className="notice">No network configuration saved yet. Please fill out the form.</div>
      )}
      {message && <div className="notice">{message}</div>}
      <form className="card stack" onSubmit={handleSubmit}>
        <label>
          Interface name
          <input value={safeConfig.interface_name} onChange={(e) => updateField('interface_name', e.target.value)} />
        </label>
        <label>
          Local IP
          <input value={safeConfig.local_ip} onChange={(e) => updateField('local_ip', e.target.value)} />
        </label>
        <label>
          Multicast groups (comma separated)
          <input value={safeConfig.multicast_groups.join(', ')} onChange={(e) => updateGroups(e.target.value)} />
        </label>
        <label>
          PD Port
          <input type="number" value={safeConfig.pd_port} onChange={(e) => updateField('pd_port', e.target.value)} />
        </label>
        <label>
          MD Port
          <input type="number" value={safeConfig.md_port} onChange={(e) => updateField('md_port', e.target.value)} />
        </label>
        <button type="submit">Save</button>
      </form>
    </div>
  );
}
